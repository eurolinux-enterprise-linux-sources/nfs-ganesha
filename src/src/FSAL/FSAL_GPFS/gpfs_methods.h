/* GPFS methods for handles
 */

#include <fcntl.h>
#include "include/gpfs_nfs.h"

/* private helpers from export
 */

struct fsal_staticfsinfo_t *gpfs_staticinfo(struct fsal_module *hdl);

/* method proto linkage to handle.c for export
 */

fsal_status_t gpfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle,
			       struct attrlist *attrs_out);

fsal_status_t gpfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out);

/*
 * GPFS internal export
 */

struct gpfs_fsal_export {
	struct fsal_export export;
	struct fsal_filesystem *root_fs;
	struct glist_head filesystems;
	bool pnfs_ds_enabled;
	bool pnfs_mds_enabled;
	bool use_acl;
};

/*
 * GPFS internal filesystem
 */
struct gpfs_filesystem {
	struct fsal_filesystem *fs;
	int root_fd;
	struct glist_head exports;
	bool up_thread_started;
	const struct fsal_up_vector *up_ops;
	pthread_t up_thread; /* upcall thread */
};

/*
 * Link GPFS file systems and exports
 * Supports a many-to-many relationship
 */
struct gpfs_filesystem_export_map {
	struct gpfs_fsal_export *exp;
	struct gpfs_filesystem *fs;
	struct glist_head on_exports;
	struct glist_head on_filesystems;
};

void gpfs_extract_fsid(struct gpfs_file_handle *fh, struct fsal_fsid__ *fsid);

void gpfs_unexport_filesystems(struct gpfs_fsal_export *exp);

fsal_status_t gpfs_merge(struct fsal_obj_handle *orig_hdl,
			 struct fsal_obj_handle *dupe_hdl);

/*
 * GPFS internal object handle
 * handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 * wrt locks, should this be a lock counter??
 * AF_UNIX sockets are strange ducks.  I personally cannot see why they
 * are here except for the ability of a client to see such an animal with
 * an 'ls' or get rid of one with an 'rm'.  You can't open them in the
 * usual file way so open_by_handle_at leads to a deadend.  To work around
 * this, we save the args that were used to mknod or lookup the socket.
 */

struct gpfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct gpfs_file_handle *handle;
	union {
		struct {
			struct fsal_share share;
			struct gpfs_fd fd;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
	} u;
};

	/* I/O management */
struct gpfs_fsal_obj_handle *alloc_handle(struct gpfs_file_handle *fh,
					  struct fsal_filesystem *fs,
					  struct attrlist *attributes,
					  const char *link_content,
					  struct fsal_export *exp_hdl);
fsal_status_t create(struct fsal_obj_handle *dir_hdl,
		     const char *name, struct attrlist *attr_in,
		     struct fsal_obj_handle **handle,
		     struct attrlist *attrs_out);
fsal_status_t gpfs_open2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state,
			 fsal_openflags_t openflags,
			 enum fsal_create_mode createmode,
			 const char *name,
			 struct attrlist *attrib_set,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **new_obj,
			 struct attrlist *attrs_out,
			 bool *caller_perm_check);
fsal_status_t gpfs_reopen2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   fsal_openflags_t openflags);
fsal_status_t gpfs_read2(struct fsal_obj_handle *obj_hdl,
			 bool bypass,
			 struct state_t *state,
			 uint64_t offset,
			 size_t buffer_size,
			 void *buffer,
			 size_t *read_amount,
			 bool *end_of_file,
			 struct io_info *info);
fsal_status_t gpfs_write2(struct fsal_obj_handle *obj_hdl,
			  bool bypass,
			  struct state_t *state,
			  uint64_t offset,
			  size_t buffer_size,
			  void *buffer,
			  size_t *wrote_amount,
			  bool *fsal_stable,
			  struct io_info *info);
fsal_status_t gpfs_commit2(struct fsal_obj_handle *obj_hdl,
			   off_t offset,
			   size_t len);
fsal_status_t gpfs_commit_fd(int my_fd, struct fsal_obj_handle *obj_hdl,
			   off_t offset,
			   size_t len);
fsal_status_t gpfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state,
			    void *owner,
			    fsal_lock_op_t lock_op,
			    fsal_lock_param_t *request_lock,
			    fsal_lock_param_t *conflicting_lock);
fsal_status_t gpfs_close2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state);
fsal_status_t gpfs_setattr2(struct fsal_obj_handle *obj_hdl,
			    bool bypass,
			    struct state_t *state,
			    struct attrlist *attrib_set);
fsal_status_t gpfs_open(struct fsal_obj_handle *obj_hdl,
			fsal_openflags_t openflags);
fsal_status_t gpfs_reopen(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags);
fsal_openflags_t gpfs_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t gpfs_read(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file);
fsal_status_t gpfs_read_plus(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file, struct io_info *info);
fsal_status_t gpfs_read_plus_fd(int my_fs,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file, struct io_info *info, int expfd);
fsal_status_t gpfs_write(struct fsal_obj_handle *obj_hdl,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable);
fsal_status_t gpfs_write_plus(struct fsal_obj_handle *obj_hdl,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable,
			 struct io_info *info);
fsal_status_t gpfs_write_plus_fd(int my_fd,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable,
			 struct io_info *info, int expfd);
fsal_status_t gpfs_seek(struct fsal_obj_handle *obj_hdl,
			 struct io_info *info);
fsal_status_t gpfs_io_advise(struct fsal_obj_handle *obj_hdl,
			 struct io_hints *hints);
fsal_status_t gpfs_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			  off_t offset, size_t len);
fsal_status_t gpfs_commit_fd(int my_fd, struct fsal_obj_handle *obj_hdl,
			     off_t offset, size_t len);
fsal_status_t gpfs_lock_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock);
fsal_status_t gpfs_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			    fsal_share_param_t request_share);
fsal_status_t gpfs_close(struct fsal_obj_handle *obj_hdl);

/* Internal GPFS method linkage to export object */
fsal_status_t
gpfs_create_export(struct fsal_module *fsal_hdl, void *parse_node,
		   struct config_error_type *err_type,
		   const struct fsal_up_vector *up_ops);

/* Multiple file descriptor methods */
struct state_t *gpfs_alloc_state(struct fsal_export *exp_hdl,
				 enum state_type state_type,
				 struct state_t *related_state);

