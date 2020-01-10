/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2013
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

/**
 * @defgroup Filesystem export management
 * @{
 */

/**
 * @file export_mgr.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief export manager
 */

#include "config.h"

#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "avltree.h"
#include "gsh_types.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "export_mgr.h"
#include "client_mgr.h"
#include "server_stats_private.h"
#include "server_stats.h"
#include "abstract_atomic.h"
#include "gsh_intrinsic.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "pnfs_utils.h"

/**
 * @brief Exports are stored in an AVL tree with front-end cache.
 *
 * @note  number of cache slots should be prime.
 */
#define EXPORT_BY_ID_CACHE_SIZE 769

struct export_by_id {
	pthread_rwlock_t lock;
	struct avltree t;
	struct avltree_node *cache[EXPORT_BY_ID_CACHE_SIZE];
};

static struct export_by_id export_by_id;

/** List of all active exports,
  * protected by export_by_id.lock
  */
static struct glist_head exportlist;

/** List of exports to be mounted in PseudoFS,
  * protected by export_by_id.lock
  */
static struct glist_head mount_work;

/** List of exports to be cleaned up on unexport,
  * protected by export_by_id.lock
  */
static struct glist_head unexport_work;

void export_add_to_mount_work(struct gsh_export *export)
{
	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);
	glist_add_tail(&mount_work, &export->exp_work);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
}

void export_add_to_unexport_work_locked(struct gsh_export *export)
{
	glist_add_tail(&unexport_work, &export->exp_work);
}

void export_add_to_unexport_work(struct gsh_export *export)
{
	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);
	export_add_to_unexport_work_locked(export);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
}

struct gsh_export *export_take_mount_work(void)
{
	struct gsh_export *export;

	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);

	export = glist_first_entry(&mount_work, struct gsh_export, exp_work);

	if (export != NULL)
		glist_del(&export->exp_work);

	PTHREAD_RWLOCK_unlock(&export_by_id.lock);

	return export;
}

struct gsh_export *export_take_unexport_work(void)
{
	struct gsh_export *export;

	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);

	export = glist_first_entry(&unexport_work, struct gsh_export, exp_work);

	if (export != NULL) {
		glist_del(&export->exp_work);
		get_gsh_export_ref(export);
	}

	PTHREAD_RWLOCK_unlock(&export_by_id.lock);

	return export;
}

/**
 * @brief Compute cache slot for an entry
 *
 * This function computes a hash slot, taking an address modulo the
 * number of cache slotes (which should be prime).
 *
 * @param k [in] Entry index value
 *
 * @return The computed offset.
 */
static inline uint16_t eid_cache_offsetof(uint16_t k)
{
	return k % EXPORT_BY_ID_CACHE_SIZE;
}

/**
 * @brief Revert export_commit()
 *
 * @param export [in] the export just inserted/committed
 */
void export_revert(struct gsh_export *export)
{
	struct avltree_node *cnode;
	void **cache_slot = (void **)
	     &(export_by_id.cache[eid_cache_offsetof(export->export_id)]);

	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);

	cnode = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
	if (&export->node_k == cnode)
		atomic_store_voidptr(cache_slot, NULL);
	avltree_remove(&export->node_k, &export_by_id.t);
	glist_del(&export->exp_list);
	glist_del(&export->exp_work);

	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	put_gsh_export(export); /* Release sentinel ref */
}

/**
 * @brief Export id comparator for AVL tree walk
 *
 */
static int export_id_cmpf(const struct avltree_node *lhs,
			  const struct avltree_node *rhs)
{
	struct gsh_export *lk, *rk;

	lk = avltree_container_of(lhs, struct gsh_export, node_k);
	rk = avltree_container_of(rhs, struct gsh_export, node_k);
	if (lk->export_id != rk->export_id)
		return (lk->export_id < rk->export_id) ? -1 : 1;
	else
		return 0;
}

/**
 * @brief Allocate a gsh_export entry.
 *
 * This is the ONLY function that should allocate gsh_exports
 *
 * @return pointer to gsh_export.
 *
 */

struct gsh_export *alloc_export(void)
{
	struct export_stats *export_st;
	struct gsh_export *export;

	export_st = gsh_calloc(1, sizeof(struct export_stats));

	export = &export_st->export;

	LogFullDebug(COMPONENT_EXPORT, "Allocated export %p", export);

	glist_init(&export->exp_state_list);
	glist_init(&export->exp_lock_list);
	glist_init(&export->exp_nlm_share_list);
	glist_init(&export->mounted_exports_list);
	glist_init(&export->clients);

	PTHREAD_RWLOCK_init(&export->lock, NULL);

	return export;
}

/**
 * @brief Free an exportlist entry.
 *
 * This is for returning exportlists not yet in the export manager.
 * Once they get inserted into the export manager, it will release it.
 */

void free_export(struct gsh_export *export)
{
	struct export_stats *export_st;

	assert(export->refcnt == 0);

	/* free resources */
	free_export_resources(export);
	export_st = container_of(export, struct export_stats, export);
	server_stats_free(&export_st->st);
	gsh_free(export_st);
	PTHREAD_RWLOCK_destroy(&export->lock);
}


/**
 * @brief Insert an export list entry into the export manager
 *
 * WARNING! This takes a pointer to the container of the exportlist.
 * The struct exports_stats is the container lots of stuff besides
 * the exportlist so only pass an object you got from alloc_exportlist.
 *
 * @param exp [IN] the exportlist entry to insert
 *
 * @retval  false on error
 */

bool insert_gsh_export(struct gsh_export *export)
{
	struct avltree_node *node;
	void **cache_slot = (void **)
	    &(export_by_id.cache[eid_cache_offsetof(export->export_id)]);

	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);
	node = avltree_insert(&export->node_k, &export_by_id.t);
	if (node) {
		/* somebody beat us to it */
		PTHREAD_RWLOCK_unlock(&export_by_id.lock);
		return false;
	}

	/* we will hold a ref starting out... */
	get_gsh_export_ref(export);

	/* update cache */
	atomic_store_voidptr(cache_slot, &export->node_k);
	glist_add_tail(&exportlist, &export->exp_list);
	get_gsh_export_ref(export);		/* == 2 */

	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return true;
}

/**
 * @brief Lookup the export manager struct for this export id
 *
 * Lookup the export manager struct by export id.
 * Export ids are assigned by the config file and carried about
 * by file handles.
 *
 * @param export_id   [IN] the export id extracted from the handle
 *
 * @return pointer to ref locked export
 */
struct gsh_export *get_gsh_export(uint16_t export_id)
{
	struct gsh_export v;
	struct avltree_node *node;
	struct gsh_export *exp;
	void **cache_slot = (void **)
	    &(export_by_id.cache[eid_cache_offsetof(export_id)]);

	v.export_id = export_id;
	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);

	/* check cache */
	node = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
	if (node) {
		exp = avltree_container_of(node, struct gsh_export, node_k);
		if (exp->export_id == export_id) {
			/* got it in 1 */
			LogDebug(COMPONENT_HASHTABLE_CACHE,
				 "export_mgr cache hit slot %d",
				 eid_cache_offsetof(export_id));
			goto out;
		}
	}

	/* fall back to AVL */
	node = avltree_lookup(&v.node_k, &export_by_id.t);
	if (node) {
		exp = avltree_container_of(node, struct gsh_export, node_k);
		/* update cache */
		atomic_store_voidptr(cache_slot, node);
	} else {
		PTHREAD_RWLOCK_unlock(&export_by_id.lock);
		return NULL;
	}

 out:
	get_gsh_export_ref(exp);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return exp;
}

/**
 * @brief Lookup the export manager struct by export path
 *
 * Gets an export entry from its path using a substring match and
 * linear search of the export list, assumes being called with
 * export manager lock held (such as from within foreach_gsh_export.
 * If path has a trailing '/', ignore it.
 *
 * @param path        [IN] the path for the entry to be found.
 * @param exact_match [IN] the path must match exactly
 *
 * @return pointer to ref counted export
 */

struct gsh_export *get_gsh_export_by_path_locked(char *path,
						 bool exact_match)
{
	struct gsh_export *export;
	struct glist_head *glist;
	int len_path = strlen(path);
	int len_export;
	struct gsh_export *ret_exp = NULL;
	int len_ret = 0;

	if (len_path > 1 && path[len_path - 1] == '/')
		len_path--;

	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, struct gsh_export, exp_list);

		len_export = strlen(export->fullpath);

		if (len_path == 0 && len_export == 1) {
			/* Special case for root match */
			ret_exp = export;
			len_ret = len_export;
			break;
		}

		/* A path shorter than the full path cannot match.
		 * Also skip if this export has a shorter path than
		 * the previous match.
		 */
		if (len_path < len_export ||
		    len_export < len_ret)
			continue;

		/* If partial match is not allowed, lengths must be the same */
		if (exact_match && len_path != len_export)
			continue;

		/* if the char in fullpath just after the end of path is not '/'
		 * it is a name token longer, i.e. /mnt/foo != /mnt/foob/
		 */
		if (len_export > 1 &&
		    path[len_export] != '/' &&
		    path[len_export] != '\0')
			continue;

		/* we agree on size, now compare the leading substring
		 */
		if (strncmp(export->fullpath, path, len_export) == 0) {
			ret_exp = export;
			len_ret = len_export;

			/* If we have found an exact match, exit loop. */
			if (len_export == len_path)
				break;
		}
	}

	if (ret_exp != NULL)
		get_gsh_export_ref(ret_exp);

	return ret_exp;
}

/**
 * @brief Lookup the export manager struct by export path
 *
 * Gets an export entry from its path using a substring match and
 * linear search of the export list.
 * If path has a trailing '/', ignore it.
 *
 * @param path        [IN] the path for the entry to be found.
 * @param exact_match [IN] the path must match exactly
 *
 * @return pointer to ref counted export
 */

struct gsh_export *get_gsh_export_by_path(char *path, bool exact_match)
{
	struct gsh_export *exp;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);

	exp = get_gsh_export_by_path_locked(path, exact_match);

	PTHREAD_RWLOCK_unlock(&export_by_id.lock);

	return exp;
}

/**
 * @brief Lookup the export manager struct by export pseudo path
 *
 * Gets an export entry from its pseudo (if it exists), assumes
 * being called with export manager lock held (such as from within
 * foreach_gsh_export.
 *
 * @param path        [IN] the path for the entry to be found.
 * @param exact_match [IN] the path must match exactly
 *
 * @return pointer to ref counted export
 */

struct gsh_export *get_gsh_export_by_pseudo_locked(char *path,
						   bool exact_match)
{
	struct gsh_export *export;
	struct glist_head *glist;
	int len_path = strlen(path);
	int len_export;
	struct gsh_export *ret_exp = NULL;
	int len_ret = 0;

	/* Ignore trailing slash in path */
	if (len_path > 1 && path[len_path - 1] == '/')
		len_path--;

	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, struct gsh_export, exp_list);

		if (export->pseudopath == NULL)
			continue;

		len_export = strlen(export->pseudopath);

		LogFullDebug(COMPONENT_EXPORT,
			     "Comparing %s %d to %s %d",
			     path, len_path,
			     export->pseudopath, len_export);

		if (len_path == 0 && len_export == 1) {
			/* Special case for Pseudo root match */
			ret_exp = export;
			len_ret = len_export;
			break;
		}

		/* A path shorter than the full path cannot match.
		 * Also skip if this export has a shorter path than
		 * the previous match.
		 */
		if (len_path < len_export ||
		    len_export < len_ret)
			continue;

		/* If partial match is not allowed, lengths must be the same */
		if (exact_match && len_path != len_export)
			continue;

		/* if the char in pseudopath just after the end of path is not
		 * '/' it is a name token longer, i.e. /mnt/foo != /mnt/foob/
		 */
		if (len_export > 1 &&
		    path[len_export] != '/' &&
		    path[len_export] != '\0')
			continue;

		/* we agree on size, now compare the leading substring
		 */
		if (strncmp(export->pseudopath, path, len_export)
		    == 0) {
			ret_exp = export;
			len_ret = len_export;

			/* If we have found an exact match, exit loop. */
			if (len_export == len_path)
				break;
		}
	}

	if (ret_exp != NULL)
		get_gsh_export_ref(ret_exp);

	return ret_exp;
}

/**
 * @brief Lookup the export manager struct by export pseudo path
 *
 * Gets an export entry from its pseudo (if it exists)
 *
 * @param path        [IN] the path for the entry to be found.
 * @param exact_match [IN] the path must match exactly
 *
 * @return pointer to ref counted export
 */

struct gsh_export *get_gsh_export_by_pseudo(char *path, bool exact_match)
{
	struct gsh_export *exp;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);

	exp = get_gsh_export_by_pseudo_locked(path, exact_match);

	PTHREAD_RWLOCK_unlock(&export_by_id.lock);

	return exp;
}

/**
 * @brief Lookup the export manager struct by export tag
 *
 * Gets an export entry from its pseudo (if it exists)
 *
 * @param path       [IN] the path for the entry to be found.
 *
 * @return pointer to ref locked export
 */

struct gsh_export *get_gsh_export_by_tag(char *tag)
{
	struct gsh_export *export;
	struct glist_head *glist;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, struct gsh_export, exp_list);

		if (export->FS_tag != NULL &&
		    !strcmp(export->FS_tag, tag))
			goto out;
	}
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return NULL;

 out:
	get_gsh_export_ref(export);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return export;
}

/**
 * @brief mount the export in pseudo FS
 *
 */

bool mount_gsh_export(struct gsh_export *exp)
{
	struct root_op_context root_op_context;
	bool rc = true;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
				NFS_V4, 0, NFS_REQUEST);

	if (!pseudo_mount_export(exp))
		rc = false;
	release_root_op_context();
	return rc;
}

/**
 * @brief Release the export management struct
 *
 * We are done with it, let it go.
 */

void put_gsh_export(struct gsh_export *export)
{
	int64_t refcount = atomic_dec_int64_t(&export->refcnt);

	if (refcount != 0) {
		assert(refcount > 0);
		return;
	}

	/* Releasing last reference */

	free_export(export);
}

/**
 * @brief Remove the export management struct
 *
 * Remove it from the AVL tree.
 */

void remove_gsh_export(uint16_t export_id)
{
	struct gsh_export v;
	struct avltree_node *node;
	struct gsh_export *export = NULL;
	void **cache_slot = (void **)
	    &(export_by_id.cache[eid_cache_offsetof(export_id)]);

	v.export_id = export_id;
	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);

	node = avltree_lookup(&v.node_k, &export_by_id.t);
	if (node) {
		struct avltree_node *cnode = (struct avltree_node *)
			atomic_fetch_voidptr(cache_slot);

		/* Remove from the AVL cache and tree */
		if (node == cnode)
			atomic_store_voidptr(cache_slot, NULL);
		avltree_remove(node, &export_by_id.t);

		export = avltree_container_of(node, struct gsh_export, node_k);

		/* Remove the export from the export list */
		glist_del(&export->exp_list);

		/* No new references will be granted. Idempotent. */
		export->export_status = EXPORT_STALE;
	}

	PTHREAD_RWLOCK_unlock(&export_by_id.lock);

	/* removal has a once-only semantic */
	if (export != NULL) {
		if (export->has_pnfs_ds) {
			/* once-only, so no need for lock here */
			export->has_pnfs_ds = false;
			pnfs_ds_remove(export->export_id, true);
		}

		/* Release sentinel reference to the export.
		 * Release of resources will occur on last reference.
		 * Which may or may not be from this call.
		 */
		put_gsh_export(export);
	}
}

/**
 * @ Walk the tree and do the callback on each node
 *
 * @param cb    [IN] Callback function
 * @param state [IN] param block to pass
 */

bool foreach_gsh_export(bool(*cb) (struct gsh_export *exp, void *state),
			void *state)
{
	struct glist_head *glist;
	struct gsh_export *export;
	int rc = true;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, struct gsh_export, exp_list);
		rc = cb(export, state);
		if (!rc)
			break;
	}
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return rc;
}

bool remove_one_export(struct gsh_export *export, void *state)
{
	export_add_to_unexport_work_locked(export);
	return true;
}

/**
 * @brief Bring down all exports in an orderly fashion.
 */

void remove_all_exports(void)
{
	struct gsh_export *export;
	struct root_op_context root_op_context;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
				NFS_V4, 0, NFS_REQUEST);

	/* Get a reference to the PseudoFS Root Export */
	export = get_gsh_export_by_pseudo("/", true);

	op_ctx->ctx_export = export;
	op_ctx->fsal_export = export->fsal_export;

	/* Clean up the whole PseudoFS */
	pseudo_unmount_export(export);

	put_gsh_export(export);

	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;

	/* Put all exports on the unexport work list.
	 * Ignore return since remove_one_export can't fail.
	 */
	(void) foreach_gsh_export(remove_one_export, NULL);

	/* Now process all the unexports */
	while (true) {
		export = export_take_unexport_work();
		if (export == NULL)
			break;

		op_ctx->ctx_export = export;
		op_ctx->fsal_export = export->fsal_export;

		unexport(export);
		put_gsh_export(export);

		op_ctx->ctx_export = NULL;
		op_ctx->fsal_export = NULL;
	}

	release_root_op_context();
}

#ifdef USE_DBUS

/* DBUS interfaces
 */

/**
 * @brief Return all IO stats of an export
 * DBUS_TYPE_ARRAY, "qs(tttttt)(tttttt)"
 */

static bool get_all_export_io(struct gsh_export *export_node, void *array_iter)
{
	struct export_stats *export_statistics;

	LogFullDebug(COMPONENT_DBUS, "export id: %i, path: %s",
		     export_node->export_id, export_node->fullpath);

	export_statistics = container_of(export_node, struct export_stats,
					 export);
	server_dbus_all_iostats(export_statistics,
				(DBusMessageIter *) array_iter);

	return true;
}

/* parse the export_id in args
 */

static bool arg_export_id(DBusMessageIter *args, uint16_t *export_id,
			  char **errormsg)
{
	bool success = true;

	if (args == NULL) {
		success = false;
		*errormsg = "message has no arguments";
	} else if (DBUS_TYPE_UINT16 != dbus_message_iter_get_arg_type(args)) {
		success = false;
		*errormsg = "arg not a 16 bit integer";
	} else {
		dbus_message_iter_get_basic(args, export_id);
	}
	return success;
}

/* DBUS export manager stats helpers
 */

static struct gsh_export *lookup_export(DBusMessageIter *args, char **errormsg)
{
	uint16_t export_id;
	struct gsh_export *export = NULL;
	bool success = true;

	success = arg_export_id(args, &export_id, errormsg);
	if (success) {
		export = get_gsh_export(export_id);
		if (export == NULL)
			*errormsg = "Export id not found";
	}
	return export;
}

/* Private state for config_errs_to_dbus to convert
 * parsing error stream in to a string of '\n' terminated
 * message lines.
 */

struct error_detail {
	char *buf;
	size_t bufsize;
	FILE *fp;
};

/**
 * @brief Report processing errors to the DBUS client.
 *
 * For now, they just go to the log (as before).
 */

static void config_errs_to_dbus(char *err, void *dest,
				struct config_error_type *err_type)
{
	struct error_detail *err_dest = dest;

	if (err_dest->fp == NULL) {
		err_dest->fp = open_memstream(&err_dest->buf,
					      &err_dest->bufsize);
		if (err_dest->fp == NULL) {
			LogCrit(COMPONENT_EXPORT,
				"Unable to allocate space for parse errors");
			return;
		}
	}
	fprintf(err_dest->fp, "%s\n", err);
}

struct showexports_state {
	DBusMessageIter export_iter;
};

/**
 * @brief Add an export either before or after the ID'd export
 *
 * This method passes a pathname in the server's local filesystem
 * that should be parsed and processed by the config_parsing module.
 * The resulting export entry is then added before or after the ID'd
 * export entry. Params are in the args iter
 *
 * @param "path"   [IN] A local path to a file with only an EXPORT {...}
 *
 * @return        true for success, false with error filled out for failure
 */

static bool gsh_export_addexport(DBusMessageIter *args,
				 DBusMessage *reply,
				 DBusError *error)
{
	int rc, exp_cnt = 0;
	bool status = true;
	char *file_path = NULL;
	char *export_expr = NULL;
	config_file_t config_struct = NULL;
	struct config_node_list *config_list, *lp, *lp_next;
	struct config_error_type err_type;
	DBusMessageIter iter;
	char *err_detail = NULL;
	struct error_detail conf_errs = {NULL, 0, NULL};

	/* Get path */
	if (dbus_message_iter_get_arg_type(args) == DBUS_TYPE_STRING)
		dbus_message_iter_get_basic(args, &file_path);
	else {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "Pathname is not a string. It is a (%c)",
			       dbus_message_iter_get_arg_type(args));
		status = false;
		goto out;
	}
	if (dbus_message_iter_next(args) &&
	    dbus_message_iter_get_arg_type(args) == DBUS_TYPE_STRING)
		dbus_message_iter_get_basic(args, &export_expr);
	else {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "expression is not a string. It is a (%c)",
			       dbus_message_iter_get_arg_type(args));
		status = false;
		goto out;
	}
	LogInfo(COMPONENT_EXPORT, "Adding export from file: %s with %s",
		file_path, export_expr);

	/* Create a memstream for parser+processing error messages */
	if (!init_error_type(&err_type))
		goto out;

	config_struct = config_ParseFile(file_path, &err_type);
	if (!config_error_is_harmless(&err_type)) {
		err_detail = err_type_str(&err_type);
		LogCrit(COMPONENT_EXPORT,
			"Error while parsing %s", file_path);
		report_config_errors(&err_type,
				     &conf_errs,
				     config_errs_to_dbus);
		if (conf_errs.fp != NULL)
			fclose(conf_errs.fp);
		dbus_set_error(error, DBUS_ERROR_INVALID_FILE_CONTENT,
			       "Error while parsing %s because of %s errors. Details:\n%s",
			       file_path,
			       err_detail != NULL ? err_detail : "unknown",
			       conf_errs.buf);
		status = false;
		goto out;
	}

	rc = find_config_nodes(config_struct, export_expr,
			       &config_list, &err_type);
	if (rc != 0) {
		LogCrit(COMPONENT_EXPORT,
			"Error finding exports: %s because %s",
			export_expr, strerror(rc));
		report_config_errors(&err_type,
				     &conf_errs,
				     config_errs_to_dbus);
		if (conf_errs.fp != NULL)
			fclose(conf_errs.fp);
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "Error finding exports: %s because %s",
			       export_expr, strerror(rc));
		status = false;
		goto out;
	}
	/* Load export entries from list */
	for (lp = config_list; lp != NULL; lp = lp_next) {
		lp_next = lp->next;
		if (status) {
			rc = load_config_from_node(lp->tree_node,
						   &add_export_param,
						   NULL,
						   false,
						   &err_type);
			if (rc == 0 || config_error_is_harmless(&err_type))
				exp_cnt++;
			else if (!err_type.exists)
				status = false;
		}
		gsh_free(lp);
	}
	report_config_errors(&err_type,
			     &conf_errs,
			     config_errs_to_dbus);
	if (conf_errs.fp != NULL)
		fclose(conf_errs.fp);
	if (status) {
		if (exp_cnt > 0) {
			size_t msg_size = sizeof("%d exports added") + 10;
			char *message;

			if (conf_errs.buf != NULL &&
			    strlen(conf_errs.buf) > 0) {
				msg_size += (strlen(conf_errs.buf)
					     + strlen(". Errors found:\n"));
				message = gsh_calloc(1, msg_size);
				snprintf(message, msg_size,
					 "%d exports added. Errors found:\n%s",
					 exp_cnt, conf_errs.buf);
			} else {
				message = gsh_calloc(1, msg_size);
				snprintf(message, msg_size,
					 "%d exports added", exp_cnt);
			}
			dbus_message_iter_init_append(reply, &iter);
			dbus_message_iter_append_basic(&iter,
						       DBUS_TYPE_STRING,
						       &message);
			gsh_free(message);
		} else if (err_type.exists) {
			LogWarn(COMPONENT_EXPORT,
				"Selected entries in %s already active!!!",
				file_path);
			dbus_set_error(error, DBUS_ERROR_INVALID_FILE_CONTENT,
				       "Selected entries in %s already active!!!",
				       file_path);
			status = false;
		} else {
			LogWarn(COMPONENT_EXPORT,
				"No usable export entry found in %s!!!",
				file_path);
			dbus_set_error(error, DBUS_ERROR_INVALID_FILE_CONTENT,
				       "No new export entries found in %s",
				       file_path);
			status = false;
		}
		goto out;
	} else {
		err_detail = err_type_str(&err_type);
		LogCrit(COMPONENT_EXPORT,
			"%d export entries in %s added because %s errors",
			exp_cnt, file_path,
			err_detail != NULL ? err_detail : "unknown");
		dbus_set_error(error,
			       DBUS_ERROR_INVALID_FILE_CONTENT,
			       "%d export entries in %s added because %s errors. Details:\n%s",
			       exp_cnt, file_path,
			       err_detail != NULL ? err_detail : "unknown",
			       conf_errs.buf);
	}

out:
	if (conf_errs.buf)
		gsh_free(conf_errs.buf);
	if (err_detail != NULL)
		gsh_free(err_detail);
	config_Free(config_struct);
	return status;
}

static struct gsh_dbus_method export_add_export = {
	.name = "AddExport",
	.method = gsh_export_addexport,
	.args =	{PATH_ARG,
		 EXPR_ARG,
		 MESSAGE_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Remove an export
 *
 * @param "id"  [IN] the id of the export to remove
 *
 * @return           As above, use DBusError to return errors.
 */

static bool gsh_export_removeexport(DBusMessageIter *args,
				    DBusMessage *reply,
				    DBusError *error)
{
	struct gsh_export *export = NULL;
	char *errormsg;
	bool rc = true;

	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		LogDebug(COMPONENT_EXPORT, "lookup_export failed with %s",
			errormsg);
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "lookup_export failed with %s",
			       errormsg);
		rc = false;
		goto out;
	} else {
		if (export->export_id == 0) {
			LogDebug(COMPONENT_EXPORT,
				"Cannot remove export with id 0");
			put_gsh_export(export);
			rc = false;
			dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
				       "Cannot remove export with id 0");
			goto out;
		}
		unexport(export);
		LogInfo(COMPONENT_EXPORT, "Removed export with id %d",
			export->export_id);

		put_gsh_export(export);
	}

out:
	return rc;
}

static struct gsh_dbus_method export_remove_export = {
	.name = "RemoveExport",
	.method = gsh_export_removeexport,
	.args = {ID_ARG,
		 END_ARG_LIST}
};

#define DISP_EXP_REPLY		\
{				\
	.name = "id",		\
	.type = "q",		\
	.direction = "out"	\
},				\
{				\
	.name = "fullpath",	\
	.type = "s",		\
	.direction = "out"	\
},				\
{				\
	.name = "pseudopath",	\
	.type = "s",		\
	.direction = "out"	\
},				\
{				\
	.name = "tag",		\
	.type = "s",		\
	.direction = "out"	\
}

/**
 * @brief Display the contents of an export
 *
 * NOTE: this is probably better done as properties.
 * the interfaces are set up for it.  This is here for now
 * but should not be considered a permanent method
 */

static bool gsh_export_displayexport(DBusMessageIter *args,
				     DBusMessage *reply,
				     DBusError *error)
{
	DBusMessageIter iter;
	struct gsh_export *export = NULL;
	char *errormsg;
	bool rc = true;
	char *path;

	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		LogDebug(COMPONENT_EXPORT, "lookup_export failed with %s",
			errormsg);
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "lookup_export failed with %s",
			       errormsg);
		rc = false;
		goto out;
	}

	/* create a reply from the message */
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_append_basic(&iter,
				       DBUS_TYPE_UINT16,
				       &export->export_id);
	path = (export->fullpath != NULL) ? export->fullpath : "";
	dbus_message_iter_append_basic(&iter,
				       DBUS_TYPE_STRING,
				       &path);
	path = (export->pseudopath != NULL) ? export->pseudopath : "";
	dbus_message_iter_append_basic(&iter,
				       DBUS_TYPE_STRING,
				       &path);
	path = (export->FS_tag != NULL) ? export->FS_tag : "";
	dbus_message_iter_append_basic(&iter,
				       DBUS_TYPE_STRING,
				       &path);

out:
	return rc;
}

static struct gsh_dbus_method export_display_export = {
	.name = "DisplayExport",
	.method = gsh_export_displayexport,
	.args = {ID_ARG,
		 DISP_EXP_REPLY,
		 END_ARG_LIST}
};

static bool export_to_dbus(struct gsh_export *exp_node, void *state)
{
	struct showexports_state *iter_state =
	    (struct showexports_state *)state;
	struct export_stats *exp;
	DBusMessageIter struct_iter;
	struct timespec last_as_ts = ServerBootTime;
	const char *path;

	exp = container_of(exp_node, struct export_stats, export);
	path = (exp_node->pseudopath != NULL) ?
		exp_node->pseudopath : exp_node->fullpath;
	timespec_add_nsecs(exp_node->last_update, &last_as_ts);
	dbus_message_iter_open_container(&iter_state->export_iter,
					 DBUS_TYPE_STRUCT, NULL, &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT16,
				       &exp_node->export_id);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &path);
	server_stats_summary(&struct_iter, &exp->st);
	dbus_append_timestamp(&struct_iter, &last_as_ts);
	dbus_message_iter_close_container(&iter_state->export_iter,
					  &struct_iter);
	return true;
}

static bool gsh_export_showexports(DBusMessageIter *args,
				   DBusMessage *reply,
				   DBusError *error)
{
	DBusMessageIter iter;
	struct showexports_state iter_state;
	struct timespec timestamp;

	now(&timestamp);
	/* create a reply from the message */
	dbus_message_iter_init_append(reply, &iter);
	dbus_append_timestamp(&iter, &timestamp);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 "(qsbbbbbbbb(tt))",
					 &iter_state.export_iter);

	(void)foreach_gsh_export(export_to_dbus, (void *)&iter_state);

	dbus_message_iter_close_container(&iter, &iter_state.export_iter);
	return true;
}

static struct gsh_dbus_method export_show_exports = {
	.name = "ShowExports",
	.method = gsh_export_showexports,
	.args = {TIMESTAMP_REPLY,
		 {
		  .name = "exports",
		  .type = "a(qsbbbbbbbb(tt))",
		  .direction = "out"},
		 END_ARG_LIST}
};

/**
 * @brief Update an export
 *
 * This method passes a pathname in the server's local filesystem
 * that should be parsed and processed by the config_parsing module.
 * The resulting export entry is then updated. Params are in the args iter
 *
 * @param "path"   [IN] A local path to a file with only an EXPORT {...}
 *
 * @return        true for success, false with error filled out for failure
 */

static bool gsh_export_update_export(DBusMessageIter *args,
				     DBusMessage *reply,
				     DBusError *error)
{
	int rc, exp_cnt = 0;
	bool status = true;
	char *file_path = NULL;
	char *export_expr = NULL;
	config_file_t config_struct = NULL;
	struct config_node_list *config_list, *lp, *lp_next;
	struct config_error_type err_type;
	DBusMessageIter iter;
	char *err_detail = NULL;
	struct error_detail conf_errs = {NULL, 0, NULL};

	/* Get path */
	if (dbus_message_iter_get_arg_type(args) == DBUS_TYPE_STRING)
		dbus_message_iter_get_basic(args, &file_path);
	else {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "Pathname is not a string. It is a (%c)",
			       dbus_message_iter_get_arg_type(args));
		status = false;
		goto out;
	}
	if (dbus_message_iter_next(args) &&
	    dbus_message_iter_get_arg_type(args) == DBUS_TYPE_STRING)
		dbus_message_iter_get_basic(args, &export_expr);
	else {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "expression is not a string. It is a (%c)",
			       dbus_message_iter_get_arg_type(args));
		status = false;
		goto out;
	}
	LogInfo(COMPONENT_EXPORT, "Adding export from file: %s with %s",
		file_path, export_expr);

	/* Create a memstream for parser+processing error messages */
	if (!init_error_type(&err_type))
		goto out;

	config_struct = config_ParseFile(file_path, &err_type);
	if (!config_error_is_harmless(&err_type)) {
		err_detail = err_type_str(&err_type);
		LogCrit(COMPONENT_EXPORT,
			"Error while parsing %s", file_path);
		report_config_errors(&err_type,
				     &conf_errs,
				     config_errs_to_dbus);
		if (conf_errs.fp != NULL)
			fclose(conf_errs.fp);
		dbus_set_error(error, DBUS_ERROR_INVALID_FILE_CONTENT,
			       "Error while parsing %s because of %s errors. Details:\n%s",
			       file_path,
			       err_detail != NULL ? err_detail : "unknown",
			       conf_errs.buf);
		status = false;
		goto out;
	}

	rc = find_config_nodes(config_struct, export_expr,
			       &config_list, &err_type);
	if (rc != 0) {
		LogCrit(COMPONENT_EXPORT,
			"Error finding exports: %s because %s",
			export_expr, strerror(rc));
		report_config_errors(&err_type,
				     &conf_errs,
				     config_errs_to_dbus);
		if (conf_errs.fp != NULL)
			fclose(conf_errs.fp);
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "Error finding exports: %s because %s",
			       export_expr, strerror(rc));
		status = false;
		goto out;
	}
	/* Update export entries from list */
	for (lp = config_list; lp != NULL; lp = lp_next) {
		lp_next = lp->next;
		if (status) {
			rc = load_config_from_node(lp->tree_node,
						   &update_export_param,
						   NULL,
						   false,
						   &err_type);
			if (rc == 0 || config_error_is_harmless(&err_type))
				exp_cnt++;
			else if (!err_type.exists)
				status = false;
		}
		gsh_free(lp);
	}
	report_config_errors(&err_type,
			     &conf_errs,
			     config_errs_to_dbus);
	if (conf_errs.fp != NULL)
		fclose(conf_errs.fp);
	if (status) {
		if (exp_cnt > 0) {
			size_t msg_size = sizeof("%d exports updated") + 10;
			char *message;

			if (conf_errs.buf != NULL &&
			    strlen(conf_errs.buf) > 0) {
				msg_size += (strlen(conf_errs.buf)
					     + strlen(". Errors found:\n"));
				message = gsh_calloc(1, msg_size);
				snprintf(message, msg_size,
					 "%d exports updated. Errors found:\n%s",
					 exp_cnt, conf_errs.buf);
			} else {
				message = gsh_calloc(1, msg_size);
				snprintf(message, msg_size,
					 "%d exports updated", exp_cnt);
			}
			dbus_message_iter_init_append(reply, &iter);
			dbus_message_iter_append_basic(&iter,
						       DBUS_TYPE_STRING,
						       &message);
			gsh_free(message);
		} else if (err_type.exists) {
			LogWarn(COMPONENT_EXPORT,
				"Selected entries in %s already active!!!",
				file_path);
			dbus_set_error(error, DBUS_ERROR_INVALID_FILE_CONTENT,
				       "Selected entries in %s already active!!!",
				       file_path);
			status = false;
		} else {
			LogWarn(COMPONENT_EXPORT,
				"No usable export entry found in %s!!!",
				file_path);
			dbus_set_error(error, DBUS_ERROR_INVALID_FILE_CONTENT,
				       "No new export entries found in %s",
				       file_path);
			status = false;
		}
		goto out;
	} else {
		err_detail = err_type_str(&err_type);
		LogCrit(COMPONENT_EXPORT,
			"%d export entries in %s updated because %s errors",
			exp_cnt, file_path,
			err_detail != NULL ? err_detail : "unknown");
		dbus_set_error(error,
			       DBUS_ERROR_INVALID_FILE_CONTENT,
			       "%d export entries in %s updated because %s errors. Details:\n%s",
			       exp_cnt, file_path,
			       err_detail != NULL ? err_detail : "unknown",
			       conf_errs.buf);
	}

out:
	if (conf_errs.buf)
		gsh_free(conf_errs.buf);
	if (err_detail != NULL)
		gsh_free(err_detail);
	config_Free(config_struct);
	return status;
}

static struct gsh_dbus_method export_update_export = {
	.name = "UpdateExport",
	.method = gsh_export_update_export,
	.args =	{PATH_ARG,
		 EXPR_ARG,
		 MESSAGE_REPLY,
		 END_ARG_LIST}
};

static struct gsh_dbus_method *export_mgr_methods[] = {
	&export_add_export,
	&export_remove_export,
	&export_display_export,
	&export_show_exports,
	&export_update_export,
	NULL
};

/* org.ganesha.nfsd.exportmgr interface
 */
static struct gsh_dbus_interface export_mgr_table = {
	.name = "org.ganesha.nfsd.exportmgr",
	.props = NULL,
	.methods = export_mgr_methods,
	.signals = NULL
};

/* org.ganesha.nfsd.exportstats interface
 */

/**
 * DBUS method to report NFSv3 I/O statistics
 *
 */

static bool get_nfsv3_export_io(DBusMessageIter *args,
				DBusMessage *reply,
				DBusError *error)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if (export_st->st.nfsv3 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv3 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v3_iostats(export_st->st.nfsv3, &iter);

	if (export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_v3_io = {
	.name = "GetNFSv3IO",
	.method = get_nfsv3_export_io,
	.args = {EXPORT_ID_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report NFSv40 I/O statistics
 *
 */

static bool get_nfsv40_export_io(DBusMessageIter *args,
				 DBusMessage *reply,
				 DBusError *error)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if (export_st->st.nfsv40 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv4.0 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v40_iostats(export_st->st.nfsv40, &iter);

	if (export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_v40_io = {
	.name = "GetNFSv40IO",
	.method = get_nfsv40_export_io,
	.args = {EXPORT_ID_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report NFSv41 I/O statistics
 *
 */

static bool get_nfsv41_export_io(DBusMessageIter *args,
				 DBusMessage *reply,
				 DBusError *error)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if (export_st->st.nfsv41 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv4.1 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v41_iostats(export_st->st.nfsv41, &iter);

	if (export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_v41_io = {
	.name = "GetNFSv41IO",
	.method = get_nfsv41_export_io,
	.args = {EXPORT_ID_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report NFSv41 layout statistics
 *
 */

static bool get_nfsv41_export_layouts(DBusMessageIter *args,
				      DBusMessage *reply,
				      DBusError *error)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if (export_st->st.nfsv41 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv4.1 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v41_layouts(export_st->st.nfsv41, &iter);

	if (export != NULL)
		put_gsh_export(export);
	return true;
}

/**
 * DBUS method to report total ops statistics
 *
 */

static bool get_nfsv_export_total_ops(DBusMessageIter *args,
				      DBusMessage *reply,
				      DBusError *error)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if (export != NULL) {
		export_st = container_of(export, struct export_stats, export);
		dbus_status_reply(&iter, success, errormsg);
		server_dbus_total_ops(export_st, &iter);
		put_gsh_export(export);
	} else {
		success = false;
		errormsg = "Export does not have any activity";
		dbus_status_reply(&iter, success, errormsg);
	}
	return true;
}

static bool get_nfsv_global_total_ops(DBusMessageIter *args,
				      DBusMessage *reply,
				      DBusError *error)
{
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	dbus_status_reply(&iter, success, errormsg);

	global_dbus_total_ops(&iter);

	return true;
}

static bool get_nfsv_global_fast_ops(DBusMessageIter *args,
				     DBusMessage *reply,
				     DBusError *error)
{
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	dbus_status_reply(&iter, success, errormsg);

	server_dbus_fast_ops(&iter);

	return true;
}

static bool show_cache_inode_stats(DBusMessageIter *args,
				   DBusMessage *reply,
				   DBusError *error)
{
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	dbus_status_reply(&iter, success, errormsg);

	mdcache_dbus_show(&iter);

	return true;
}

static struct gsh_dbus_method export_show_v41_layouts = {
	.name = "GetNFSv41Layouts",
	.method = get_nfsv41_export_layouts,
	.args = {EXPORT_ID_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 LAYOUTS_REPLY,
		 END_ARG_LIST}
};

#ifdef _USE_9P
/**
 * DBUS method to report 9p I/O statistics
 *
 */
static bool get_9p_export_io(DBusMessageIter *args,
			     DBusMessage *reply,
			     DBusError *error)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if (export_st->st._9p == NULL) {
			success = false;
			errormsg = "Export does not have any 9p activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_9p_iostats(export_st->st._9p, &iter);

	if (export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_9p_io = {
	.name = "Get9pIO",
	.method = get_9p_export_io,
	.args = {EXPORT_ID_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report 9p protocol operation statistics
 *
 */
static bool get_9p_export_op_stats(DBusMessageIter *args,
				   DBusMessage *reply,
				   DBusError *error)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	u8 opcode;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if (export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if (export_st->st._9p == NULL) {
			success = false;
			errormsg = "Export does not have any 9p activity";
		}
	}
	dbus_message_iter_next(args);
	if (success)
		success = arg_9p_op(args, &opcode, &errormsg);
	dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_9p_opstats(export_st->st._9p, opcode, &iter);

	if (export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_9p_op_stats = {
	.name = "Get9pOpStats",
	.method = get_9p_export_op_stats,
	.args = {EXPORT_ID_ARG,
		 _9P_OP_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 OP_STATS_REPLY,
		 END_ARG_LIST}
};
#endif

static struct gsh_dbus_method export_show_total_ops = {
	.name = "GetTotalOPS",
	.method = get_nfsv_export_total_ops,
	.args = {EXPORT_ID_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 TOTAL_OPS_REPLY,
		 END_ARG_LIST}
};

static struct gsh_dbus_method global_show_total_ops = {
	.name = "GetGlobalOPS",
	.method = get_nfsv_global_total_ops,
	.args = {STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 TOTAL_OPS_REPLY,
		 END_ARG_LIST}
};

static struct gsh_dbus_method global_show_fast_ops = {
	.name = "GetFastOPS",
	.method = get_nfsv_global_fast_ops,
	.args = {STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 TOTAL_OPS_REPLY,
		 END_ARG_LIST}
};

static struct gsh_dbus_method cache_inode_show = {
	.name = "ShowCacheInode",
	.method = show_cache_inode_stats,
	.args = {STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 TOTAL_OPS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Report all IO stats of all exports in one call
 *
 * @return
 *	status
 *	error message
 *	time
 *	array of (
 *		export id
 *		string containing the protocol version
 *		read statistics structure
 *			(requested, transferred, total, errors, latency,
 *			queue wait)
 *		write statistics structure
 *			(requested, transferred, total, errors, latency,
 *			queue wait)
 *	)
 */
static bool get_nfs_io(DBusMessageIter *args,
				DBusMessage *message,
				DBusError *error)
{
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter reply_iter, array_iter;
	struct timespec timestamp;

	/* create a reply iterator from the message */
	dbus_message_iter_init_append(message, &reply_iter);

	/* status and timestamp reply */
	dbus_status_reply(&reply_iter, success, errormsg);
	now(&timestamp);
	dbus_append_timestamp(&reply_iter, &timestamp);

	/* create an array container iterator and loop over all exports */
	dbus_message_iter_open_container(&reply_iter, DBUS_TYPE_ARRAY,
					 NFS_ALL_IO_REPLY_ARRAY_TYPE,
					 &array_iter);
	(void) foreach_gsh_export(&get_all_export_io, (void *) &array_iter);
	dbus_message_iter_close_container(&reply_iter, &array_iter);

	return true;
}

static struct gsh_dbus_method export_show_all_io = {
	.name = "GetNFSIO",
	.method = get_nfs_io,
	.args = {STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 NFS_ALL_IO_REPLY,
		 END_ARG_LIST}
};

static struct gsh_dbus_method *export_stats_methods[] = {
	&export_show_v3_io,
	&export_show_v40_io,
	&export_show_v41_io,
	&export_show_v41_layouts,
	&export_show_total_ops,
#ifdef _USE_9P
	&export_show_9p_io,
	&export_show_9p_op_stats,
#endif
	&global_show_total_ops,
	&global_show_fast_ops,
	&cache_inode_show,
	&export_show_all_io,
	NULL
};

static struct gsh_dbus_interface export_stats_table = {
	.name = "org.ganesha.nfsd.exportstats",
	.methods = export_stats_methods
};

/* DBUS list of interfaces on /org/ganesha/nfsd/ExportMgr
 */

static struct gsh_dbus_interface *export_interfaces[] = {
	&export_mgr_table,
	&export_stats_table,
	NULL
};

void dbus_export_init(void)
{
	gsh_dbus_register_path("ExportMgr", export_interfaces);
}
#endif				/* USE_DBUS */

/**
 * @brief Initialize export manager
 */

void export_pkginit(void)
{
	pthread_rwlockattr_t rwlock_attr;

	pthread_rwlockattr_init(&rwlock_attr);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&rwlock_attr,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	PTHREAD_RWLOCK_init(&export_by_id.lock, &rwlock_attr);
	avltree_init(&export_by_id.t, export_id_cmpf, 0);
	memset(&export_by_id.cache, 0, sizeof(export_by_id.cache));

	glist_init(&exportlist);
	glist_init(&mount_work);
	glist_init(&unexport_work);
}

/** @} */
