/*
 * Copyright (C) 2009-2012 the libgit2 contributors
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */
#ifndef INCLUDE_git_diff_h__
#define INCLUDE_git_diff_h__

#include "common.h"
#include "types.h"
#include "oid.h"
#include "tree.h"
#include "refs.h"

/**
 * @file git2/diff.h
 * @brief Git tree and file differencing routines.
 *
 * Calculating diffs is generally done in two phases: building a diff list
 * then traversing the diff list.  This makes is easier to share logic
 * across the various types of diffs (tree vs tree, workdir vs index, etc.),
 * and also allows you to insert optional diff list post-processing phases,
 * such as rename detected, in between the steps.  When you are done with a
 * diff list object, it must be freed.
 *
 * @ingroup Git
 * @{
 */
GIT_BEGIN_DECL

/**
 * Flags for diff options.  A combination of these flags can be passed
 * in via the `flags` value in the `git_diff_options`.
 */
enum {
	GIT_DIFF_NORMAL = 0,
	GIT_DIFF_REVERSE = (1 << 0),
	GIT_DIFF_FORCE_TEXT = (1 << 1),
	GIT_DIFF_IGNORE_WHITESPACE = (1 << 2),
	GIT_DIFF_IGNORE_WHITESPACE_CHANGE = (1 << 3),
	GIT_DIFF_IGNORE_WHITESPACE_EOL = (1 << 4),
	GIT_DIFF_IGNORE_SUBMODULES = (1 << 5),
	GIT_DIFF_PATIENCE = (1 << 6),
	GIT_DIFF_INCLUDE_IGNORED = (1 << 7),
	GIT_DIFF_INCLUDE_UNTRACKED = (1 << 8),
	GIT_DIFF_INCLUDE_UNMODIFIED = (1 << 9),
	GIT_DIFF_RECURSE_UNTRACKED_DIRS = (1 << 10),
	GIT_DIFF_DISABLE_PATHSPEC_MATCH = (1 << 11),
};

/**
 * Structure describing options about how the diff should be executed.
 *
 * Setting all values of the structure to zero will yield the default
 * values.  Similarly, passing NULL for the options structure will
 * give the defaults.  The default values are marked below.
 *
 * @todo Most of the parameters here are not actually supported at this time.
 */
typedef struct {
	uint32_t flags;				/**< defaults to GIT_DIFF_NORMAL */
	uint16_t context_lines;		/**< defaults to 3 */
	uint16_t interhunk_lines;	/**< defaults to 0 */
	char *old_prefix;			/**< defaults to "a" */
	char *new_prefix;			/**< defaults to "b" */
	git_strarray pathspec;		/**< defaults to show all paths */
} git_diff_options;

/**
 * The diff list object that contains all individual file deltas.
 */
typedef struct git_diff_list git_diff_list;

enum {
	GIT_DIFF_FILE_VALID_OID  = (1 << 0),
	GIT_DIFF_FILE_FREE_PATH  = (1 << 1),
	GIT_DIFF_FILE_BINARY     = (1 << 2),
	GIT_DIFF_FILE_NOT_BINARY = (1 << 3),
	GIT_DIFF_FILE_FREE_DATA  = (1 << 4),
	GIT_DIFF_FILE_UNMAP_DATA = (1 << 5)
};

/**
 * What type of change is described by a git_diff_delta?
 */
typedef enum {
	GIT_DELTA_UNMODIFIED = 0,
	GIT_DELTA_ADDED = 1,
	GIT_DELTA_DELETED = 2,
	GIT_DELTA_MODIFIED = 3,
	GIT_DELTA_RENAMED = 4,
	GIT_DELTA_COPIED = 5,
	GIT_DELTA_IGNORED = 6,
	GIT_DELTA_UNTRACKED = 7
} git_delta_t;

/**
 * Description of one side of a diff.
 */
typedef struct {
	git_oid oid;
	char *path;
	git_off_t size;
	unsigned int flags;
	uint16_t mode;
} git_diff_file;

/**
 * Description of changes to one file.
 *
 * When iterating over a diff list object, this will generally be passed to
 * most callback functions and you can use the contents to understand
 * exactly what has changed.
 *
 * Under some circumstances, not all fields will be filled in, but the code
 * generally tries to fill in as much as possible.  One example is that the
 * "binary" field will not actually look at file contents if you do not
 * pass in hunk and/or line callbacks to the diff foreach iteration function.
 * It will just use the git attributes for those files.
 */
typedef struct {
	git_diff_file old_file;
	git_diff_file new_file;
	git_delta_t   status;
	unsigned int  similarity; /**< for RENAMED and COPIED, value 0-100 */
	int           binary;
} git_diff_delta;

/**
 * When iterating over a diff, callback that will be made per file.
 */
typedef int (*git_diff_file_fn)(
	void *cb_data,
	git_diff_delta *delta,
	float progress);

/**
 * Structure describing a hunk of a diff.
 */
typedef struct {
	int old_start;
	int old_lines;
	int new_start;
	int new_lines;
} git_diff_range;

/**
 * When iterating over a diff, callback that will be made per hunk.
 */
typedef int (*git_diff_hunk_fn)(
	void *cb_data,
	git_diff_delta *delta,
	git_diff_range *range,
	const char *header,
	size_t header_len);

/**
 * Line origin constants.
 *
 * These values describe where a line came from and will be passed to
 * the git_diff_data_fn when iterating over a diff.  There are some
 * special origin constants at the end that are used for the text
 * output callbacks to demarcate lines that are actually part of
 * the file or hunk headers.
 */
enum {
	/* These values will be sent to `git_diff_data_fn` along with the line */
	GIT_DIFF_LINE_CONTEXT   = ' ',
	GIT_DIFF_LINE_ADDITION  = '+',
	GIT_DIFF_LINE_DELETION  = '-',
	GIT_DIFF_LINE_ADD_EOFNL = '\n', /**< DEPRECATED */
	GIT_DIFF_LINE_DEL_EOFNL = '\0', /**< LF was removed at end of file */

	/* The following values will only be sent to a `git_diff_data_fn` when
	 * the content of a diff is being formatted (eg. through
	 * git_diff_print_patch() or git_diff_print_compact(), for instance).
	 */
	GIT_DIFF_LINE_FILE_HDR  = 'F',
	GIT_DIFF_LINE_HUNK_HDR  = 'H',
	GIT_DIFF_LINE_BINARY    = 'B'
};

/**
 * When iterating over a diff, callback that will be made per text diff
 * line. In this context, the provided range will be NULL.
 *
 * When printing a diff, callback that will be made to output each line
 * of text.  This uses some extra GIT_DIFF_LINE_... constants for output
 * of lines of file and hunk headers.
 */
typedef int (*git_diff_data_fn)(
	void *cb_data,
	git_diff_delta *delta,
	git_diff_range *range,
	char line_origin, /**< GIT_DIFF_LINE_... value from above */
	const char *content,
	size_t content_len);

/** @name Diff List Generator Functions
 *
 * These are the functions you would use to create (or destroy) a
 * git_diff_list from various objects in a repository.
 */
/**@{*/

/**
 * Deallocate a diff list.
 */
GIT_EXTERN(void) git_diff_list_free(git_diff_list *diff);

/**
 * Compute a difference between two tree objects.
 *
 * This is equivalent to `git diff <treeish> <treeish>`
 *
 * @param repo The repository containing the trees.
 * @param opts Structure with options to influence diff or NULL for defaults.
 * @param old_tree A git_tree object to diff from.
 * @param new_tree A git_tree object to diff to.
 * @param diff A pointer to a git_diff_list pointer that will be allocated.
 */
GIT_EXTERN(int) git_diff_tree_to_tree(
	git_repository *repo,
	const git_diff_options *opts, /**< can be NULL for defaults */
	git_tree *old_tree,
	git_tree *new_tree,
	git_diff_list **diff);

/**
 * Compute a difference between a tree and the index.
 *
 * This is equivalent to `git diff --cached <treeish>` or if you pass
 * the HEAD tree, then like `git diff --cached`.
 *
 * @param repo The repository containing the tree and index.
 * @param opts Structure with options to influence diff or NULL for defaults.
 * @param old_tree A git_tree object to diff from.
 * @param diff A pointer to a git_diff_list pointer that will be allocated.
 */
GIT_EXTERN(int) git_diff_index_to_tree(
	git_repository *repo,
	const git_diff_options *opts, /**< can be NULL for defaults */
	git_tree *old_tree,
	git_diff_list **diff);

/**
 * Compute a difference between the working directory and the index.
 *
 * This matches the `git diff` command.  See the note below on
 * `git_diff_workdir_to_tree` for a discussion of the difference between
 * `git diff` and `git diff HEAD` and how to emulate a `git diff <treeish>`
 * using libgit2.
 *
 * @param repo The repository.
 * @param opts Structure with options to influence diff or NULL for defaults.
 * @param diff A pointer to a git_diff_list pointer that will be allocated.
 */
GIT_EXTERN(int) git_diff_workdir_to_index(
	git_repository *repo,
	const git_diff_options *opts, /**< can be NULL for defaults */
	git_diff_list **diff);

/**
 * Compute a difference between the working directory and a tree.
 *
 * This is *NOT* the same as `git diff <treeish>`.  Running `git diff HEAD`
 * or the like actually uses information from the index, along with the tree
 * and workdir dir info.
 *
 * This function returns strictly the differences between the tree and the
 * files contained in the working directory, regardless of the state of
 * files in the index.  It may come as a surprise, but there is no direct
 * equivalent in core git.
 *
 * To emulate `git diff <treeish>`, you should call both
 * `git_diff_index_to_tree` and `git_diff_workdir_to_index`, then call
 * `git_diff_merge` on the results.  That will yield a `git_diff_list` that
 * matches the git output.
 *
 * If this seems confusing, take the case of a file with a staged deletion
 * where the file has then been put back into the working dir and modified.
 * The tree-to-workdir diff for that file is 'modified', but core git would
 * show status 'deleted' since there is a pending deletion in the index.
 *
 * @param repo The repository containing the tree.
 * @param opts Structure with options to influence diff or NULL for defaults.
 * @param old_tree A git_tree object to diff from.
 * @param diff A pointer to a git_diff_list pointer that will be allocated.
 */
GIT_EXTERN(int) git_diff_workdir_to_tree(
	git_repository *repo,
	const git_diff_options *opts, /**< can be NULL for defaults */
	git_tree *old_tree,
	git_diff_list **diff);

/**
 * Merge one diff list into another.
 *
 * This merges items from the "from" list into the "onto" list.  The
 * resulting diff list will have all items that appear in either list.
 * If an item appears in both lists, then it will be "merged" to appear
 * as if the old version was from the "onto" list and the new version
 * is from the "from" list (with the exception that if the item has a
 * pending DELETE in the middle, then it will show as deleted).
 *
 * @param onto Diff to merge into.
 * @param from Diff to merge.
 */
GIT_EXTERN(int) git_diff_merge(
	git_diff_list *onto,
	const git_diff_list *from);

/**@}*/


/** @name Diff List Processor Functions
 *
 * These are the functions you apply to a diff list to process it
 * or read it in some way.
 */
/**@{*/

/**
 * Iterate over a diff list issuing callbacks.
 *
 * This will iterate through all of the files described in a diff.  You
 * should provide a file callback to learn about each file.
 *
 * The "hunk" and "line" callbacks are optional, and the text diff of the
 * files will only be calculated if they are not NULL.  Of course, these
 * callbacks will not be invoked for binary files on the diff list or for
 * files whose only changed is a file mode change.
 *
 * Returning a non-zero value from any of the callbacks will terminate
 * the iteration and cause this return `GIT_EUSER`.
 *
 * @param diff A git_diff_list generated by one of the above functions.
 * @param cb_data Reference pointer that will be passed to your callbacks.
 * @param file_cb Callback function to make per file in the diff.
 * @param hunk_cb Optional callback to make per hunk of text diff.  This
 *                callback is called to describe a range of lines in the
 *                diff.  It will not be issued for binary files.
 * @param line_cb Optional callback to make per line of diff text.  This
 *                same callback will be made for context lines, added, and
 *                removed lines, and even for a deleted trailing newline.
 * @return 0 on success, GIT_EUSER on non-zero callback, or error code
 */
GIT_EXTERN(int) git_diff_foreach(
	git_diff_list *diff,
	void *cb_data,
	git_diff_file_fn file_cb,
	git_diff_hunk_fn hunk_cb,
	git_diff_data_fn line_cb);

/**
 * Iterate over a diff generating text output like "git diff --name-status".
 *
 * Returning a non-zero value from the callbacks will terminate the
 * iteration and cause this return `GIT_EUSER`.
 *
 * @param diff A git_diff_list generated by one of the above functions.
 * @param cb_data Reference pointer that will be passed to your callback.
 * @param print_cb Callback to make per line of diff text.
 * @return 0 on success, GIT_EUSER on non-zero callback, or error code
 */
GIT_EXTERN(int) git_diff_print_compact(
	git_diff_list *diff,
	void *cb_data,
	git_diff_data_fn print_cb);

/**
 * Iterate over a diff generating text output like "git diff".
 *
 * This is a super easy way to generate a patch from a diff.
 *
 * Returning a non-zero value from the callbacks will terminate the
 * iteration and cause this return `GIT_EUSER`.
 *
 * @param diff A git_diff_list generated by one of the above functions.
 * @param cb_data Reference pointer that will be passed to your callbacks.
 * @param print_cb Callback function to output lines of the diff.  This
 *                 same function will be called for file headers, hunk
 *                 headers, and diff lines.  Fortunately, you can probably
 *                 use various GIT_DIFF_LINE constants to determine what
 *                 text you are given.
 * @return 0 on success, GIT_EUSER on non-zero callback, or error code
 */
GIT_EXTERN(int) git_diff_print_patch(
	git_diff_list *diff,
	void *cb_data,
	git_diff_data_fn print_cb);

/**@}*/


/*
 * Misc
 */

/**
 * Directly run a text diff on two blobs.
 *
 * Compared to a file, a blob lacks some contextual information. As such,
 * the `git_diff_file` parameters of the callbacks will be filled
 * accordingly to the following: `mode` will be set to 0, `path` will be set
 * to NULL. When dealing with a NULL blob, `oid` will be set to 0.
 *
 * When at least one of the blobs being dealt with is binary, the
 * `git_diff_delta` binary attribute will be set to 1 and no call to the
 * hunk_cb nor line_cb will be made.
 *
 * @return 0 on success, GIT_EUSER on non-zero callback, or error code
 */
GIT_EXTERN(int) git_diff_blobs(
	git_blob *old_blob,
	git_blob *new_blob,
	git_diff_options *options,
	void *cb_data,
	git_diff_file_fn file_cb,
	git_diff_hunk_fn hunk_cb,
	git_diff_data_fn line_cb);

GIT_END_DECL

/** @} */

#endif
