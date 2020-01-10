/**
 * @file handle.c
 * @brief GPFS object (file|dir) handle object
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include "config.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "gpfs_methods.h"


/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */

#define ATTR_GPFS_ALLOC_HANDLE (ATTR_TYPE | ATTR_FILEID | ATTR_FSID)

struct gpfs_fsal_obj_handle *alloc_handle(struct gpfs_file_handle *fh,
					 struct fsal_filesystem *fs,
					 struct attrlist *attributes,
					 const char *link_content,
					 struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_export *myself =
	    container_of(exp_hdl, struct gpfs_fsal_export, export);
	struct gpfs_fsal_obj_handle *hdl =
	    gsh_calloc(1, sizeof(struct gpfs_fsal_obj_handle) +
			  sizeof(struct gpfs_file_handle));

	hdl->handle = (struct gpfs_file_handle *)&hdl[1];
	hdl->obj_handle.fs = fs;
	memcpy(hdl->handle, fh, sizeof(struct gpfs_file_handle));
	hdl->obj_handle.type = attributes->type;
	if (hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd.fd = -1;	/* no open on this yet */
		hdl->u.file.fd.openflags = FSAL_O_CLOSED;
	} else if (hdl->obj_handle.type == SYMBOLIC_LINK
		   && link_content != NULL) {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = gsh_malloc(len);
		memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
	}

	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl, attributes->type);
	hdl->obj_handle.fsid = attributes->fsid;
	hdl->obj_handle.fileid = attributes->fileid;
	gpfs_handle_ops_init(&hdl->obj_handle.obj_ops);
	if (myself->pnfs_mds_enabled)
		handle_ops_pnfs(&hdl->obj_handle.obj_ops);

	return hdl;
}

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */
static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle,
			    struct attrlist *attrs_out)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct attrlist attrib;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	struct fsal_filesystem *fs;

	*handle = NULL;		/* poison it first */
	fs = parent->fs;
	if (!path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;
	if (!parent->obj_ops.handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	if (parent->fsal != parent->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 parent->fsal->name, parent->fs->fsal->name);
		retval = EXDEV;
		goto hdlerr;
	}

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status = GPFSFSAL_lookup(op_ctx, parent, path, &attrib, fh, &fs);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, fs, &attrib, NULL, op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}

	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 hdlerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

/* create
 * create a regular file and set its attributes
 */
fsal_status_t create(struct fsal_obj_handle *dir_hdl,
		     const char *name, struct attrlist *attr_in,
		     struct fsal_obj_handle **handle,
		     struct attrlist *attrs_out)
{
	struct gpfs_fsal_obj_handle *hdl;
	fsal_status_t status;
	/* Use a separate attrlist to getch the actual attributes into */
	struct attrlist attrib;

	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status =
	    GPFSFSAL_create(dir_hdl, name, op_ctx, attr_in->mode, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, &attrib, NULL, op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}

	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attr_in,
			     struct fsal_obj_handle **handle,
			     struct attrlist *attrs_out)
{
	struct gpfs_fsal_obj_handle *hdl;
	fsal_status_t status;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	/* Use a separate attrlist to getch the actual attributes into */
	struct attrlist attrib;

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status =
	    GPFSFSAL_mkdir(dir_hdl, name, op_ctx, attr_in->mode, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, &attrib, NULL, op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}
	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attr_in->valid_mask, ATTR_MODE);

	if (attr_in->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops.setattr2(*handle, false, NULL,
						     attr_in);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*handle)->obj_ops.release(*handle);
			*handle = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
	FSAL_SET_MASK(attr_in->valid_mask, ATTR_MODE);

	return status;
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      fsal_dev_t *dev,
			      struct attrlist *attr_in,
			      struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	/* Use a separate attrlist to getch the actual attributes into */
	struct attrlist attrib;

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status =
	    GPFSFSAL_mknode(dir_hdl, name, op_ctx, attr_in->mode, nodetype, dev,
			    fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, &attrib, NULL, op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}
	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attr_in->valid_mask, ATTR_MODE);

	if (attr_in->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops.setattr2(*handle, false, NULL,
						     attr_in);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*handle)->obj_ops.release(*handle);
			*handle = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
	FSAL_SET_MASK(attr_in->valid_mask, ATTR_MODE);

	return status;
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */
static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attr_in,
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	/* Use a separate attrlist to getch the actual attributes into */
	struct attrlist attrib;

	*handle = NULL;		/* poison it first */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status = GPFSFSAL_symlink(dir_hdl, name, link_path, op_ctx,
				  attr_in->mode, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, &attrib, link_path,
			   op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}
	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attr_in->valid_mask, ATTR_MODE);

	if (attr_in->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops.setattr2(*handle, false, NULL,
						     attr_in);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				      fsal_err_txt(status));
			(*handle)->obj_ops.release(*handle);
			*handle = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
	FSAL_SET_MASK(attr_in->valid_mask, ATTR_MODE);

	return status;
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *myself = NULL;
	fsal_status_t status;

	if (obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if (refresh) {		/* lazy load or LRU'd storage */
		size_t retlink;
		char link_buff[PATH_MAX];

		retlink = PATH_MAX - 1;

		if (myself->u.symlink.link_content != NULL) {
			gsh_free(myself->u.symlink.link_content);
			myself->u.symlink.link_content = NULL;
			myself->u.symlink.link_size = 0;
		}

		status =
		    GPFSFSAL_readlink(obj_hdl, op_ctx, link_buff, &retlink);
		if (FSAL_IS_ERROR(status))
			return status;

		myself->u.symlink.link_content = gsh_malloc(retlink + 1);

		memcpy(myself->u.symlink.link_content, link_buff, retlink);
		myself->u.symlink.link_content[retlink] = '\0';
		myself->u.symlink.link_size = retlink + 1;
	}
	if (myself->u.symlink.link_content == NULL) {
		fsal_error = ERR_FSAL_FAULT;	/* probably a better error?? */
		goto out;
	}
	link_content->len = myself->u.symlink.link_size;
	link_content->addr = gsh_malloc(link_content->len);

	memcpy(link_content->addr, myself->u.symlink.link_content,
	       link_content->len);

 out:

	return fsalstat(fsal_error, retval);
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	status = GPFSFSAL_link(destdir_hdl, myself->handle, name, op_ctx);

	return status;
}

#define BUF_SIZE 1024
/**
 * read_dirents
 * read the directory and call through the callback function for
 * each entry.
 * @param dir_hdl [IN] the directory to read
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker true == end of dir
 */
static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *myself;
	int dirfd;
	fsal_status_t status;
	off_t seekloc = 0;
	int bpos, cnt, nread;
	struct dirent64 *dentry;
	char buf[BUF_SIZE];
	struct gpfs_filesystem *gpfs_fs;

	if (whence != NULL)
		seekloc = (off_t) *whence;

	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = dir_hdl->fs->private_data;

	status = fsal_internal_handle2fd_at(gpfs_fs->root_fd, myself->handle,
					    &dirfd, O_RDONLY | O_DIRECTORY, 0);
	if (dirfd < 0)
		return status;

	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if (seekloc < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto done;
	}
	cnt = 0;
	do {
		nread = syscall(SYS_getdents64, dirfd, buf, BUF_SIZE);
		if (nread < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto done;
		}
		if (nread == 0)
			break;
		for (bpos = 0; bpos < nread;) {
			struct fsal_obj_handle *hdl;
			struct attrlist attrs;
			bool cb_rc;

			dentry = (struct dirent64 *)(buf + bpos);
			if (strcmp(dentry->d_name, ".") == 0
			    || strcmp(dentry->d_name, "..") == 0)
				goto skip;	/* must skip '.' and '..' */

			fsal_prepare_attrs(&attrs, attrmask);

			status = lookup(dir_hdl, dentry->d_name, &hdl, &attrs);
			if (FSAL_IS_ERROR(status)) {
				fsal_error = status.major;
				goto done;
			}

			/* callback to cache inode */
			cb_rc = cb(dentry->d_name, hdl, &attrs, dir_state,
				   (fsal_cookie_t) dentry->d_off);

			fsal_release_attrs(&attrs);

			if (!cb_rc)
				goto done;
 skip:
			bpos += dentry->d_reclen;
			cnt++;
		}
	} while (nread > 0);

	*eof = true;
 done:
	close(dirfd);

	return fsalstat(fsal_error, retval);
}

static fsal_status_t renamefile(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	fsal_status_t status;

	status =
	    GPFSFSAL_rename(olddir_hdl, old_name, newdir_hdl, new_name,
			    op_ctx);
	return status;
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
			      obj_handle);

	return GPFSFSAL_getattrs(op_ctx->fsal_export,
				 obj_hdl->fs->private_data,
				 op_ctx, myself->handle,
				 attrs);
}

static fsal_status_t getxattrs(struct fsal_obj_handle *obj_hdl,
				xattrname4 *xa_name,
				xattrvalue4 *xa_value)
{
	int rc;
	int errsv;
	struct getxattr_arg gxarg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_filesystem *gpfs_fs = obj_hdl->fs->private_data;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	gxarg.mountdirfd = gpfs_fs->root_fd;
	gxarg.handle = myself->handle;
	gxarg.name_len = xa_name->utf8string_len;
	gxarg.name = xa_name->utf8string_val;
	gxarg.value_len = xa_value->utf8string_len;
	gxarg.value = xa_value->utf8string_val;

	rc = gpfs_ganesha(OPENHANDLE_GETXATTRS, &gxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"GETXATTRS returned rc %d errsv %d", rc, errsv);

		if (errsv == ERANGE)
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		if (errsv == ENODATA)
			return fsalstat(ERR_FSAL_NOENT, 0);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	LogDebug(COMPONENT_FSAL,
		"GETXATTRS returned value %.*s len %d rc %d",
		gxarg.value_len, (char *)gxarg.value, gxarg.value_len, rc);

	xa_value->utf8string_len = gxarg.value_len;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t setxattrs(struct fsal_obj_handle *obj_hdl,
				setxattr_type4 sa_type,
				xattrname4 *xa_name,
				xattrvalue4 *xa_value)
{
	int rc;
	int errsv;
	struct setxattr_arg sxarg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_filesystem *gpfs_fs = obj_hdl->fs->private_data;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	sxarg.mountdirfd = gpfs_fs->root_fd;
	sxarg.handle = myself->handle;
	sxarg.name_len = xa_name->utf8string_len;
	sxarg.name = xa_name->utf8string_val;
	sxarg.value_len = xa_value->utf8string_len;
	sxarg.value = xa_value->utf8string_val;

	rc = gpfs_ganesha(OPENHANDLE_SETXATTRS, &sxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"SETXATTRS returned rc %d errsv %d",
			rc, errsv);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t removexattrs(struct fsal_obj_handle *obj_hdl,
				xattrname4 *xa_name)
{
	int rc;
	int errsv;
	struct removexattr_arg rxarg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_filesystem *gpfs_fs = obj_hdl->fs->private_data;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	rxarg.mountdirfd = gpfs_fs->root_fd;
	rxarg.handle = myself->handle;
	rxarg.name_len = xa_name->utf8string_len;
	rxarg.name = xa_name->utf8string_val;

	rc = gpfs_ganesha(OPENHANDLE_REMOVEXATTRS, &rxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"REMOVEXATTRS returned rc %d errsv %d",
			rc, errsv);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t listxattrs(struct fsal_obj_handle *obj_hdl,
				count4 la_maxcount,
				nfs_cookie4 *la_cookie,
				verifier4 *la_cookieverf,
				bool_t *lr_eof,
				xattrlist4 *lr_names)
{
	int rc;
	int errsv;
	char *name, *next, *end, *val, *valstart;
	int entryCount = 0;
	char *buf = NULL;
	struct listxattr_arg lxarg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_filesystem *gpfs_fs = obj_hdl->fs->private_data;
	component4 *entry = lr_names->entries;

	val = (char *)entry + la_maxcount;
	valstart = val;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);
	#define MAXCOUNT (1024*64)
	buf = gsh_malloc(MAXCOUNT);

	lxarg.mountdirfd = gpfs_fs->root_fd;
	lxarg.handle = myself->handle;
	lxarg.cookie = 0; /* For now gpfs doesn't support cookie */
	lxarg.verifier = *((uint64_t *)la_cookieverf);
	lxarg.eof = false;
	lxarg.name_len = MAXCOUNT;
	lxarg.names = buf;

	LogFullDebug(COMPONENT_FSAL,
		"in cookie %llu len %d cookieverf %llx",
		(unsigned long long)lxarg.cookie, la_maxcount,
		(unsigned long long)lxarg.verifier);

	rc = gpfs_ganesha(OPENHANDLE_LISTXATTRS, &lxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"LISTXATTRS returned rc %d errsv %d",
			rc, errsv);
		gsh_free(buf);
		if (errsv == ERANGE)
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	if (!lxarg.eof) {
		errsv = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to get xattr.");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	/* Only return names that the caller can read via getxattr */
	name = buf;
	end = buf + rc;
	entry->utf8string_len = 0;
	entry->utf8string_val = NULL;

	while (name < end) {
		next = strchr(name, '\0');
		next += 1;

		LogDebug(COMPONENT_FSAL,
		"nameP %s at offset %td", name, (next - name));

		if (entryCount >= *la_cookie) {
			if ((((char *)entry - (char *)lr_names->entries) +
			     sizeof(component4) > la_maxcount) ||
			     ((val - valstart)+(next - name) > la_maxcount)) {
				gsh_free(buf);
				*lr_eof = false;

				lr_names->entryCount = entryCount - *la_cookie;
				*la_cookie += entryCount;
				LogFullDebug(COMPONENT_FSAL,
				   "out1 cookie %llu off %td eof %d cookieverf %llx",
				   (unsigned long long)*la_cookie,
				   (next - name), *lr_eof,
				   (unsigned long long)*
				   ((uint64_t *)la_cookieverf));

				if (lr_names->entryCount == 0)
					return fsalstat(ERR_FSAL_TOOSMALL, 0);
				return fsalstat(ERR_FSAL_NO_ERROR, 0);
			}
			entry->utf8string_len = next - name;
			entry->utf8string_val = val;
			memcpy(entry->utf8string_val, name,
						entry->utf8string_len);

			LogFullDebug(COMPONENT_FSAL,
				"entry %d val %p at %p len %d at %p name %s",
				entryCount, val, entry, entry->utf8string_len,
				entry->utf8string_val, entry->utf8string_val);

			val += entry->utf8string_len;
			entry += 1;
		}
		/* Advance to next name in original buffer */
		name = next;
		entryCount += 1;
	}
	lr_names->entryCount = entryCount - *la_cookie;
	*la_cookie = 0;
	*lr_eof = true;

	gsh_free(buf);

	LogFullDebug(COMPONENT_FSAL,
		"out2 cookie %llu eof %d cookieverf %llx",
		(unsigned long long)*la_cookie, *lr_eof,
		(unsigned long long)*((uint64_t *)la_cookieverf));

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * NOTE: this is done under protection of the attributes rwlock in cache entry.
 */
static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	fsal_status_t status;

	status = GPFSFSAL_setattrs(obj_hdl, op_ctx, attrs);

	return status;
}

/*
 * NOTE: this is done under protection of the attributes rwlock in cache entry.
 */
fsal_status_t gpfs_setattr2(struct fsal_obj_handle *obj_hdl,
				   bool bypass,
				   struct state_t *state,
				   struct attrlist *attrs)
{
	fsal_status_t status;

	status = GPFSFSAL_setattrs(obj_hdl, op_ctx, attrs);

	return status;
}

/* file_unlink
 * unlink the named file in the directory
 */
static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *obj_hdl,
				 const char *name)
{
	fsal_status_t status;

	status = GPFSFSAL_unlink(dir_hdl, name, op_ctx);

	return status;
}

/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */
static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   fsal_digesttype_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	const struct gpfs_fsal_obj_handle *myself;
	struct gpfs_file_handle *fh;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself =
	    container_of(obj_hdl, const struct gpfs_fsal_obj_handle,
			 obj_handle);
	fh = myself->handle;

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = gpfs_sizeof_handle(fh);
		if (fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, fh, fh_size);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;
	LogFullDebug(COMPONENT_FSAL,
		"FSAL fh_size %zu type %d", fh_size, output_type);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	LogMajor(COMPONENT_FSAL,
		 "Space too small for handle.  need %zu, have %zu",
		 fh_size, fh_desc->len);

	return fsalstat(ERR_FSAL_TOOSMALL, 0);
}

/**
 * handle_to_key
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 */
static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = myself->handle->handle_key_size;
}

/*
 * release
 * release our export first so they know we are gone
 */
static void release(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself;
	object_file_type_t type = obj_hdl->type;

	LogFullDebug(COMPONENT_FSAL, "type %d", type);
	if (type == REGULAR_FILE)
		gpfs_close(obj_hdl);

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	fsal_obj_handle_fini(obj_hdl);

	if (type == SYMBOLIC_LINK) {
		if (myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	}
	gsh_free(myself);
}

/* gpfs_share_op
 */
static fsal_status_t share_op(struct fsal_obj_handle *obj_hdl,
			      void *p_owner,
			      fsal_share_param_t request_share)
{
	fsal_status_t status;
	int fd, mntfd;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	mntfd = fd = myself->u.file.fd.fd;

	status = GPFSFSAL_share_op(mntfd, fd, p_owner, request_share);

	return status;
}

/* gpfs_fs_locations
 */
static fsal_status_t gpfs_fs_locations(struct fsal_obj_handle *obj_hdl,
					struct fs_locations4 *fs_locs)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
			      obj_handle);

	status = GPFSFSAL_fs_loc(op_ctx->fsal_export,
				obj_hdl->fs->private_data,
				op_ctx, myself->handle,
				fs_locs);

	return status;
}

/**
 *
 *  @param ops Object operations
*/
void gpfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->create = create;
	ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->open = gpfs_open;
	ops->reopen = gpfs_reopen;
	ops->fs_locations = gpfs_fs_locations;
	ops->status = gpfs_status;
	ops->read = gpfs_read;
	ops->read_plus = gpfs_read_plus;
	ops->write = gpfs_write;
	ops->write_plus = gpfs_write_plus;
	ops->seek = gpfs_seek;
	ops->io_advise = gpfs_io_advise;
	ops->commit = gpfs_commit;
	ops->lock_op = gpfs_lock_op;
	ops->share_op = share_op;
	ops->close = gpfs_close;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
	handle_ops_pnfs(ops);
	ops->getxattrs = getxattrs;
	ops->setxattrs = setxattrs;
	ops->removexattrs = removexattrs;
	ops->listxattrs = listxattrs;
	ops->open2 = gpfs_open2;
	ops->reopen2 = gpfs_reopen2;
	ops->read2 = gpfs_read2;
	ops->write2 = gpfs_write2;
	ops->commit2 = gpfs_commit2;
	ops->setattr2 = gpfs_setattr2;
	ops->close2 = gpfs_close2;
	ops->lock_op2 = gpfs_lock_op2;
	ops->merge = gpfs_merge;
}

/**
 *  @param exp_hdl Handle
 *  @param path Path
 *  @param handle Reference to handle
 *
 *  modelled on old api except we don't stuff attributes.
 *  @return Status of operation
 */
fsal_status_t gpfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle,
			       struct attrlist *attrs_out)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t fsal_status;
	int retval = 0;
	int dir_fd;
	int exp_fd = 0;
	struct fsal_filesystem *fs;
	struct gpfs_fsal_obj_handle *hdl;
	struct attrlist attributes;
	gpfsfsal_xstat_t buffxstat;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	struct fsal_fsid__ fsid;
	struct gpfs_fsal_export *gpfs_export;

	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	*handle = NULL;	/* poison it */

	dir_fd = open_dir_by_path_walk(-1, path, &buffxstat.buffstat);

	fsal_prepare_attrs(&attributes, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attributes.request_mask |= attrs_out->request_mask;

	if (dir_fd < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not open directory for path %s",
			path);
		retval = -dir_fd;
		goto errout;
	}

	fsal_status = fsal_internal_fd2handle(dir_fd, fh, &exp_fd);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	gpfs_export = container_of(exp_hdl, struct gpfs_fsal_export, export);

	fsal_status = fsal_get_xstat_by_handle(dir_fd, fh, &buffxstat,
					       NULL, false,
					       (attributes.request_mask &
							ATTR_ACL) != 0);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;
	fsal_status = gpfsfsal_xstat_2_fsal_attributes(&buffxstat, &attributes,
						       gpfs_export->use_acl);
	LogFullDebug(COMPONENT_FSAL,
		     "fsid=0x%016"PRIx64".0x%016"PRIx64,
		     attributes.fsid.major,
		     attributes.fsid.minor);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	close(dir_fd);

	gpfs_extract_fsid(fh, &fsid);

	fs = lookup_fsid(&fsid, GPFS_FSID_TYPE);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find file system for path %s",
			path);
		retval = ENOENT;
		goto errout;
	}
	if (fs->fsal != exp_hdl->fsal) {
		LogInfo(COMPONENT_FSAL,
			"File system for path %s did not belong to FSAL %s",
			path, exp_hdl->fsal->name);
		retval = EACCES;
		goto errout;
	}

	LogDebug(COMPONENT_FSAL,
		 "filesystem %s for path %s",
		 fs->path, path);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, fs, &attributes, NULL, exp_hdl);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attributes, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attributes);
	}

	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	retval = errno;
	close(dir_fd);

 errout:

	/* Done with attributes */
	fsal_release_attrs(&attributes);

	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

/**
 * @brief create GPFS handle
 *
 * @param exp_hdl export handle
 * @param hdl_desc handle description
 * @param handle object handle
 * @return status
 *
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in cache_inode etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket, nor reliably on block or
 * character special devices.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */
fsal_status_t gpfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out)
{
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh;
	struct attrlist attrib;
	char *link_content = NULL;
	ssize_t retlink = PATH_MAX - 1;
	char link_buff[PATH_MAX];
	struct fsal_fsid__ fsid;
	struct fsal_filesystem *fs;
	struct gpfs_filesystem *gpfs_fs;

	*handle = NULL;		/* poison it first */
	if ((hdl_desc->len > (sizeof(struct gpfs_file_handle))))
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len); /* struct aligned copy */

	gpfs_extract_fsid(fh, &fsid);

	fs = lookup_fsid(&fsid, GPFS_FSID_TYPE);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find filesystem for fsid=0x%016"PRIx64
			".0x%016"PRIx64" from handle",
			fsid.major, fsid.minor);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	if (fs->fsal != exp_hdl->fsal) {
		LogInfo(COMPONENT_FSAL,
			"Non GPFS filesystem fsid=0x%016"PRIx64
			".0x%016"PRIx64" from handle",
			fsid.major, fsid.minor);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	gpfs_fs = fs->private_data;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status = GPFSFSAL_getattrs(exp_hdl, gpfs_fs, op_ctx, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	if (attrib.type == SYMBOLIC_LINK) {	/* I could lazy eval this... */

		status = fsal_readlink_by_handle(gpfs_fs->root_fd, fh,
						 link_buff, &retlink);
		if (FSAL_IS_ERROR(status))
			return status;

		if (retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if (retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			return fsalstat(posix2fsal_error(retval), retval);
		}
		link_buff[retlink] = '\0';
		link_content = link_buff;
	}

	hdl = alloc_handle(fh, fs, &attrib, link_content, exp_hdl);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}

	*handle = &hdl->obj_handle;

	return status;
}
