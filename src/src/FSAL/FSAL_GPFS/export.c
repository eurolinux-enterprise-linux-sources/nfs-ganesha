/** @file export.c
 *  @brief GPFS FSAL module export functions.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#include "config.h"

#include <fcntl.h>
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statfs.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "gpfs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "pnfs_utils.h"
#include "mdcache.h"
#include "include/gpfs.h"

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_export *myself =
	    container_of(exp_hdl, struct gpfs_fsal_export, export);

	gpfs_unexport_filesystems(myself);
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself);		/* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	fsal_status_t status;
	struct statfs buffstatgpfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct gpfs_filesystem *gpfs_fs;

	if (!infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	gpfs_fs = obj_hdl->fs->private_data;

	status = GPFSFSAL_statfs(gpfs_fs->root_fd, obj_hdl, &buffstatgpfs);
	if (FSAL_IS_ERROR(status))
		return status;

	infop->total_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_blocks;
	infop->free_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bfree;
	infop->avail_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bavail;
	infop->total_files = buffstatgpfs.f_files;
	infop->free_files = buffstatgpfs.f_ffree;
	infop->avail_files = buffstatgpfs.f_ffree;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

 out:
	return fsalstat(fsal_error, 0);
}

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;
	attrmask_t supported_mask;
	struct gpfs_fsal_export *gpfs_export;

	gpfs_export = container_of(exp_hdl, struct gpfs_fsal_export, export);

	info = gpfs_staticinfo(exp_hdl->fsal);

	supported_mask = fsal_supported_attrs(info);

	/* Fixup supported_mask to indicate if ACL is actually supported for
	 * this export.
	 */
	if (gpfs_export->use_acl)
		supported_mask |= ATTR_ACL;
	else
		supported_mask &= ~ATTR_ACL;

	return supported_mask;
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_xattr_access_rights(info);
}

/* get_quota
 * return quotas for this export.
 * path could cross a lower mount boundary which could
 * mask lower mount values with those of the export root
 * if this is a real issue, we can scan each time with setmntent()
 * better yet, compare st_dev of the file with st_dev of root_fd.
 * on linux, can map st_dev -> /proc/partitions name -> /dev/<name>
 */

static fsal_status_t get_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       int quota_id,
			       fsal_quota_t *pquota)
{
	struct gpfs_fsal_export *myself;
	gpfs_quotaInfo_t fs_quota;
	struct stat path_stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct quotactl_arg args;
	int errsv;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS get_quota, fstat: root_path: %s, errno=(%d) %s",
			 myself->root_fs->path,
			 errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if ((major(path_stat.st_dev) != myself->root_fs->dev.major) ||
	    (minor(path_stat.st_dev) != myself->root_fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS get_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->root_fs->path, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto out;
	}
	memset((void *)&fs_quota, 0, sizeof(gpfs_quotaInfo_t));
	args.pathname = filepath;
	args.cmd = GPFS_QCMD(Q_GETQUOTA, quota_type);
	args.qid = quota_id;
	args.bufferP = (void *) &fs_quota;

	fsal_set_credentials(op_ctx->creds);
	retval = gpfs_ganesha(OPENHANDLE_QUOTA, &args);
	errsv = errno;
	fsal_restore_ganesha_credentials();

	if (retval < 0) {
		fsal_error = posix2fsal_error(errsv);
		retval = errsv;
		goto out;
	}
	pquota->bhardlimit = fs_quota.blockHardLimit;
	pquota->bsoftlimit = fs_quota.blockSoftLimit;
	pquota->curblocks = fs_quota.blockUsage;
	pquota->fhardlimit = fs_quota.inodeHardLimit;
	pquota->fsoftlimit = fs_quota.inodeSoftLimit;
	pquota->curfiles = fs_quota.inodeUsage;
	pquota->btimeleft = fs_quota.blockGraceTime;
	pquota->ftimeleft = fs_quota.inodeGraceTime;
	pquota->bsize = 1024;

 out:
	return fsalstat(fsal_error, retval);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       int quota_id,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	struct gpfs_fsal_export *myself;
	gpfs_quotaInfo_t fs_quota;
	struct stat path_stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct quotactl_arg args;
	int errsv;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS set_quota, fstat: root_path: %s, errno=(%d) %s",
			 myself->root_fs->path,
			 errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if ((major(path_stat.st_dev) != myself->root_fs->dev.major) ||
	    (minor(path_stat.st_dev) != myself->root_fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->root_fs->path, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto err;
	}
	memset((char *)&fs_quota, 0, sizeof(gpfs_quotaInfo_t));
	if (pquota->bhardlimit != 0) {
		fs_quota.blockHardLimit = pquota->bhardlimit;
	}
	if (pquota->bsoftlimit != 0) {
		fs_quota.blockSoftLimit = pquota->bsoftlimit;
	}
	if (pquota->fhardlimit != 0) {
		fs_quota.inodeHardLimit = pquota->fhardlimit;
	}
	if (pquota->fsoftlimit != 0) {
		fs_quota.inodeSoftLimit = pquota->fsoftlimit;
	}
	if (pquota->btimeleft != 0) {
		fs_quota.blockGraceTime = pquota->btimeleft;
	}
	if (pquota->ftimeleft != 0) {
		fs_quota.inodeGraceTime = pquota->ftimeleft;
	}
	args.pathname = filepath;
	args.cmd = GPFS_QCMD(Q_SETQUOTA, quota_type);
	args.qid = quota_id;
	args.bufferP = (void *) &fs_quota;

	fsal_set_credentials(op_ctx->creds);
	retval = gpfs_ganesha(OPENHANDLE_QUOTA, &args);
	errsv = errno;
	fsal_restore_ganesha_credentials();

	if (retval < 0) {
		fsal_error = posix2fsal_error(errsv);
		retval = errsv;
		goto err;
	}
	if (presquota != NULL)
		return get_quota(exp_hdl, filepath,
				 quota_type, quota_id, presquota);

 err:
	return fsalstat(fsal_error, retval);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t gpfs_extract_handle(struct fsal_export *exp_hdl,
					 fsal_digesttype_t in_type,
					 struct gsh_buffdesc *fh_desc,
					 int flags)
{
	struct gpfs_file_handle *hdl;
	size_t fh_size = 0;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct gpfs_file_handle *)fh_desc->addr;
	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		hdl->handle_size = bswap_16(hdl->handle_size);
		hdl->handle_type = bswap_16(hdl->handle_type);
		hdl->handle_version = bswap_16(hdl->handle_version);
		hdl->handle_key_size = bswap_16(hdl->handle_key_size);
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		hdl->handle_size = bswap_16(hdl->handle_size);
		hdl->handle_type = bswap_16(hdl->handle_type);
		hdl->handle_version = bswap_16(hdl->handle_version);
		hdl->handle_key_size = bswap_16(hdl->handle_key_size);
#endif
	}
	fh_size = gpfs_sizeof_handle(hdl);
	LogFullDebug(COMPONENT_FSAL,
	  "flags 0x%X size %d type %d ver %d key_size %d FSID 0x%X:%X fh_size %zu",
	   flags, hdl->handle_size, hdl->handle_type, hdl->handle_version,
	   hdl->handle_key_size, hdl->handle_fsid[0], hdl->handle_fsid[1],
	   fh_size);

	if (fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %zu, got %zu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = hdl->handle_key_size;	/* pass back the key size */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** \var GPFS_write_verifier
 *  @brief NFS V4 write verifier
 */
verifier4 GPFS_write_verifier;

static void gpfs_verifier(struct fsal_export *exp_hdl,
			  struct gsh_buffdesc *verf_desc)
{
	memcpy(verf_desc->addr, &GPFS_write_verifier, verf_desc->len);
}

/**
 *  @brief set the global GPFS_write_verfier according to \a verifier
 *  @param verfier verifier4 type
 */
void set_gpfs_verifier(verifier4 *verifier)
{
	memcpy(&GPFS_write_verifier, verifier, sizeof(verifier4));
}

/**
 *  @brief overwrite vector entries with the methods that we support
 *  @param ops tpye of struct export_ops
 */
void gpfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = gpfs_lookup_path;
	ops->extract_handle = gpfs_extract_handle;
	ops->create_handle = gpfs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_lease_time = fs_lease_time;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->fs_xattr_access_rights = fs_xattr_access_rights;
	ops->get_quota = get_quota;
	ops->set_quota = set_quota;
	ops->get_write_verifier = gpfs_verifier;
	ops->alloc_state = gpfs_alloc_state;
}

static void free_gpfs_filesystem(struct gpfs_filesystem *gpfs_fs)
{
	if (gpfs_fs->root_fd >= 0)
		close(gpfs_fs->root_fd);
	gsh_free(gpfs_fs);
}

/**
 *  @brief Extract major from from fsid
 *  @param fh GPFS file handle
 *  @param fsid FSAL ID
 */
void gpfs_extract_fsid(struct gpfs_file_handle *fh, struct fsal_fsid__ *fsid)
{
	memcpy(&fsid->major, fh->handle_fsid, sizeof(fsid->major));
	fsid->minor = 0;
}

/**
 *  @brief Open root fd
 *  @param gpfs_fs GPFS filesystem
 *  @return 0(zero) on success, otherwise error.
 */
int open_root_fd(struct gpfs_filesystem *gpfs_fs)
{
	struct fsal_fsid__ fsid;
	int retval;
	fsal_status_t status;
	struct gpfs_file_handle *fh;

	fh = alloca(sizeof(*fh));
	memset(fh, 0, sizeof(*fh));

	gpfs_fs->root_fd = open(gpfs_fs->fs->path, O_RDONLY | O_DIRECTORY);

	if (gpfs_fs->root_fd < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Could not open GPFS mount point %s: rc = %s (%d)",
			 gpfs_fs->fs->path, strerror(retval), retval);
		return retval;
	}

	status = fsal_internal_get_handle_at(gpfs_fs->root_fd,
					     gpfs_fs->fs->path, fh,
					     0, &gpfs_fs->root_fd);

	if (FSAL_IS_ERROR(status)) {
		retval = status.minor;
		LogMajor(COMPONENT_FSAL,
			 "Get root handle for %s failed with %s (%d)",
			 gpfs_fs->fs->path, strerror(retval), retval);
		goto errout;
	}

	gpfs_extract_fsid(fh, &fsid);

	retval = re_index_fs_fsid(gpfs_fs->fs, GPFS_FSID_TYPE, &fsid);

	if (retval < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not re-index GPFS file system fsid for %s",
			gpfs_fs->fs->path);
		retval = -retval;
		goto errout;
	}

	return retval;

errout:

	close(gpfs_fs->root_fd);
	gpfs_fs->root_fd = -1;

	return retval;
}

/**
 *  @brief Claim GPFS filesystem
 *  @param fs FSAL filesystem
 *  @param exp FSAL export
 *  @return 0(zero) on success, otherwise error.
 */
int gpfs_claim_filesystem(struct fsal_filesystem *fs, struct fsal_export *exp)
{
	struct gpfs_filesystem *gpfs_fs = NULL;
	int retval;
	struct gpfs_fsal_export *myself;
	struct gpfs_filesystem_export_map *map = NULL;
	pthread_attr_t attr_thr;

	myself = container_of(exp, struct gpfs_fsal_export, export);

	if (strcmp(fs->type, "gpfs") != 0) {
		LogInfo(COMPONENT_FSAL,
			"Attempt to claim non-GPFS filesystem %s",
			fs->path);
		return ENXIO;
	}

	map = gsh_calloc(1, sizeof(*map));

	if (fs->fsal != NULL) {
		gpfs_fs = fs->private_data;
		if (gpfs_fs == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Something wrong with export, fs %s appears already claimed but doesn't have private data",
				fs->path);
			retval = EINVAL;
			goto errout;
		}

		goto already_claimed;
	}

	if (fs->private_data != NULL) {
			LogCrit(COMPONENT_FSAL,
				"Something wrong with export, fs %s was not claimed but had non-NULL private",
				fs->path);
	}

	gpfs_fs = gsh_calloc(1, sizeof(*gpfs_fs));

	glist_init(&gpfs_fs->exports);
	gpfs_fs->root_fd = -1;

	gpfs_fs->fs = fs;

	retval = open_root_fd(gpfs_fs);

	if (retval != 0) {
		if (retval == ENOTTY) {
			LogInfo(COMPONENT_FSAL,
				"file system %s is not exportable with %s",
				fs->path, exp->fsal->name);
			retval = ENXIO;
		}
		goto errout;
	}

	memset(&attr_thr, 0, sizeof(attr_thr));

	if (pthread_attr_init(&attr_thr) != 0)
		LogCrit(COMPONENT_THREAD, "can't init pthread's attributes");

	if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
		LogCrit(COMPONENT_THREAD, "can't set pthread's scope");

	if (pthread_attr_setdetachstate(&attr_thr,
					PTHREAD_CREATE_JOINABLE) != 0)
		LogCrit(COMPONENT_THREAD, "can't set pthread's join state");

	if (pthread_attr_setstacksize(&attr_thr, 2116488) != 0)
		LogCrit(COMPONENT_THREAD, "can't set pthread's stack size");

	gpfs_fs->up_ops = exp->up_ops;
	retval = pthread_create(&gpfs_fs->up_thread,
				&attr_thr,
				GPFSFSAL_UP_Thread,
				gpfs_fs);

	if (retval != 0) {
		retval = errno;
		LogCrit(COMPONENT_THREAD,
			"Could not create GPFSFSAL_UP_Thread, error = %d (%s)",
			retval, strerror(retval));
		goto errout;
	}

	fs->private_data = gpfs_fs;

already_claimed:

	/* Now map the file system and export */
	map->fs = gpfs_fs;
	map->exp = myself;
	glist_add_tail(&gpfs_fs->exports, &map->on_exports);
	glist_add_tail(&myself->filesystems, &map->on_filesystems);

	return 0;

errout:

	if (map != NULL)
		gsh_free(map);

	if (gpfs_fs != NULL)
		free_gpfs_filesystem(gpfs_fs);

	return retval;
}

/**
 *  @brief Unclaim filesystem
 *  @param fs FSAL filesystem
 */
void gpfs_unclaim_filesystem(struct fsal_filesystem *fs)
{
	struct gpfs_filesystem *gpfs_fs = fs->private_data;
	struct glist_head *glist, *glistn;
	struct gpfs_filesystem_export_map *map;
	struct callback_arg callback;
	int reason;
	int rc;

	if (gpfs_fs != NULL) {
		glist_for_each_safe(glist, glistn, &gpfs_fs->exports) {
			map = glist_entry(glist,
					  struct gpfs_filesystem_export_map,
					  on_exports);

			/* Remove this file system from mapping */
			glist_del(&map->on_filesystems);
			glist_del(&map->on_exports);

			if (map->exp->root_fs == fs) {
				LogInfo(COMPONENT_FSAL,
					"Removing root_fs %s from GPFS export",
					fs->path);
			}

			/* And free it */
			gsh_free(map);
		}

		/* Terminate GPFS upcall thread */
		callback.mountdirfd = gpfs_fs->root_fd;
		reason = THREAD_STOP;
		callback.reason = &reason;
		rc = gpfs_ganesha(OPENHANDLE_THREAD_UPDATE, &callback);
		if (rc)
			LogCrit(COMPONENT_FSAL,
				"Unable to stop upcall thread for %s, fd=%d, errno=%d",
				fs->path, gpfs_fs->root_fd, errno);
		else
			LogFullDebug(COMPONENT_FSAL, "Thread STOP successful");
		pthread_join(gpfs_fs->up_thread, NULL);
		free_gpfs_filesystem(gpfs_fs);
		fs->private_data = NULL;
	}

	LogInfo(COMPONENT_FSAL,
		"GPFS Unclaiming %s",
		fs->path);
}

/**
 *  @brief Unexport filesystem
 *  @param exp FSAL export
 */
void gpfs_unexport_filesystems(struct gpfs_fsal_export *exp)
{
	struct glist_head *glist, *glistn;
	struct gpfs_filesystem_export_map *map;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	glist_for_each_safe(glist, glistn, &exp->filesystems) {
		map = glist_entry(glist,
				  struct gpfs_filesystem_export_map,
				  on_filesystems);

		/* Remove this export from mapping */
		glist_del(&map->on_filesystems);
		glist_del(&map->on_exports);

		if (glist_empty(&map->fs->exports)) {
			LogInfo(COMPONENT_FSAL,
				"GPFS is no longer exporting filesystem %s",
				map->fs->fs->path);
			unclaim_fs(map->fs->fs);
		}

		/* And free it */
		gsh_free(map);
	}

	PTHREAD_RWLOCK_unlock(&fs_lock);
}

/**
 * @brief create_export
 *
 *  Create an export point and return a handle to it to be kept
 *  in the export list.
 *  First lookup the fsal, then create the export and then put the fsal back.
 *  returns the export with one reference taken.
 *
 *  @return FSAL status
 */
fsal_status_t gpfs_create_export(struct fsal_module *fsal_hdl,
				 void *parse_node,
				 struct config_error_type *err_type,
				 const struct fsal_up_vector *up_ops)
{
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct gpfs_fsal_export *myself;
	struct readlink_arg varg;
	struct gpfs_filesystem *gpfs_fs;
	gpfsfsal_xstat_t buffxstat;
	int rc;

	myself = gsh_calloc(1, sizeof(struct gpfs_fsal_export));

	glist_init(&myself->filesystems);

	status.minor = fsal_internal_version();
	LogInfo(COMPONENT_FSAL, "GPFS get version is %d options 0x%X id %d",
		status.minor,
		op_ctx->export_perms ?
		op_ctx->export_perms->options : 0,
		op_ctx->ctx_export->export_id);

	fsal_export_init(&myself->export);
	gpfs_export_ops_init(&myself->export.exp_ops);

	status.minor = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if (status.minor != 0) {
		status.major = posix2fsal_error(status.minor);
		goto errout;	/* seriously bad */
	}
	myself->export.fsal = fsal_hdl;
	op_ctx->fsal_export = &myself->export;

	/* Stack MDCACHE on top */
	status = mdcache_export_init(up_ops, &myself->export.up_ops);
	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL, "MDCACHE creation failed for GPFS");
		goto detach;
	}

	status.minor = resolve_posix_filesystem(op_ctx->ctx_export->fullpath,
						fsal_hdl, &myself->export,
						gpfs_claim_filesystem,
						gpfs_unclaim_filesystem,
						&myself->root_fs);

	if (status.minor != 0) {
		LogCrit(COMPONENT_FSAL,
			"resolve_posix_filesystem(%s) returned %s (%d)",
			op_ctx->ctx_export->fullpath,
			strerror(status.minor), status.minor);
		status.major = posix2fsal_error(status.minor);
		goto uninit;
	}
	gpfs_fs = myself->root_fs->private_data;
	gpfs_fs->root_fd = open_dir_by_path_walk(-1,
			   op_ctx->ctx_export->fullpath, &buffxstat.buffstat);
	varg.fd = gpfs_fs->root_fd;
	varg.buffer = (char *)&GPFS_write_verifier;
	rc = gpfs_ganesha(OPENHANDLE_GET_VERIFIER, &varg);
	if (rc != 0)
		LogCrit(COMPONENT_FSAL,
		    "OPENHANDLE_GET_VERIFIER failed with rc = %d", rc);

	/* if the nodeid has not been obtained, get it now */
	if (!g_nodeid) {
		struct grace_period_arg gpa;
		int nodeid;

		gpfs_fs = myself->root_fs->private_data;
		gpa.mountdirfd = gpfs_fs->root_fd;

		nodeid = gpfs_ganesha(OPENHANDLE_GET_NODEID, &gpa);
		if (nodeid > 0) {
			g_nodeid = nodeid;
			LogFullDebug(COMPONENT_FSAL, "nodeid %d", g_nodeid);
		} else
			LogCrit(COMPONENT_FSAL,
			    "OPENHANDLE_GET_NODEID failed rc %d", nodeid);
	}
	myself->pnfs_ds_enabled =
	    myself->export.exp_ops.fs_supports(&myself->export,
					    fso_pnfs_ds_supported);
	myself->pnfs_mds_enabled =
	    myself->export.exp_ops.fs_supports(&myself->export,
					    fso_pnfs_mds_supported);
	if (myself->pnfs_ds_enabled) {
		struct fsal_pnfs_ds *pds = NULL;

		status = fsal_hdl->m_ops.
			fsal_pnfs_ds(fsal_hdl, parse_node, &pds);
		if (status.major != ERR_FSAL_NO_ERROR)
			goto uninit;

		/* special case: server_id matches export_id */
		pds->id_servers = op_ctx->ctx_export->export_id;
		pds->mds_export = op_ctx->ctx_export;
		pds->mds_fsal_export = op_ctx->fsal_export;

		if (!pnfs_ds_insert(pds)) {
			LogCrit(COMPONENT_CONFIG,
				"Server id %d already in use.",
				pds->id_servers);
			status.major = ERR_FSAL_EXIST;
			fsal_pnfs_ds_fini(pds);
			gsh_free(pds);
			goto uninit;
		}

		LogInfo(COMPONENT_FSAL,
			"gpfs_fsal_create: pnfs ds was enabled for [%s]",
			op_ctx->ctx_export->fullpath);
		export_ops_pnfs(&myself->export.exp_ops);
	}
	myself->use_acl = !op_ctx_export_has_option(EXPORT_OPTION_DISABLE_ACL);

	return status;

uninit:
	mdcache_export_uninit();
detach:
	fsal_detach_export(fsal_hdl, &myself->export.exports);
errout:
	free_export_ops(&myself->export);
	gsh_free(myself);		/* elvis has left the building */
	return status;
}
