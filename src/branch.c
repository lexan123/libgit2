/*
 * Copyright (C) 2009-2012 the libgit2 contributors
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "common.h"
#include "commit.h"
#include "tag.h"
#include "config.h"
#include "refspec.h"

#include "git2/branch.h"

static int retrieve_branch_reference(
	git_reference **branch_reference_out,
	git_repository *repo,
	const char *branch_name,
	int is_remote)
{
	git_reference *branch;
	int error = -1;
	char *prefix;
	git_buf ref_name = GIT_BUF_INIT;

	*branch_reference_out = NULL;

	prefix = is_remote ? GIT_REFS_REMOTES_DIR : GIT_REFS_HEADS_DIR;

	if (git_buf_joinpath(&ref_name, prefix, branch_name) < 0)
		goto cleanup;

	if ((error = git_reference_lookup(&branch, repo, ref_name.ptr)) < 0) {
		giterr_set(GITERR_REFERENCE,
			"Cannot locate %s branch '%s'.", is_remote ? "remote-tracking" : "local", branch_name);
		goto cleanup;
	}

	*branch_reference_out = branch;

cleanup:
	git_buf_free(&ref_name);
	return error;
}

static int create_error_invalid(const char *msg)
{
	giterr_set(GITERR_INVALID, "Cannot create branch - %s", msg);
	return -1;
}

int git_branch_create(
		git_reference **ref_out,
		git_repository *repository,
		const char *branch_name,
		const git_object *target,
		int force)
{
	git_otype target_type = GIT_OBJ_BAD;
	git_object *commit = NULL;
	git_reference *branch = NULL;
	git_buf canonical_branch_name = GIT_BUF_INIT;
	int error = -1;

	assert(branch_name && target && ref_out);
	assert(git_object_owner(target) == repository);

	target_type = git_object_type(target);

	switch (target_type)
	{
	case GIT_OBJ_TAG:
		if (git_tag_peel(&commit, (git_tag *)target) < 0)
			goto cleanup;

		if (git_object_type(commit) != GIT_OBJ_COMMIT) {
			create_error_invalid("The given target does not resolve to a commit");
			goto cleanup;
		}
		break;

	case GIT_OBJ_COMMIT:
		commit = (git_object *)target;
		break;

	default:
		return create_error_invalid("Only git_tag and git_commit objects are valid targets.");
	}

	if (git_buf_joinpath(&canonical_branch_name, GIT_REFS_HEADS_DIR, branch_name) < 0)
		goto cleanup;

	if (git_reference_create_oid(&branch, repository,
		git_buf_cstr(&canonical_branch_name), git_object_id(commit), force) < 0)
		goto cleanup;

	*ref_out = branch;
	error = 0;

cleanup:
	if (target_type == GIT_OBJ_TAG)
		git_object_free(commit);

	git_buf_free(&canonical_branch_name);
	return error;
}

int git_branch_delete(git_repository *repo, const char *branch_name, git_branch_t branch_type)
{
	git_reference *branch = NULL;
	git_reference *head = NULL;
	int error;

	assert(repo && branch_name);
	assert((branch_type == GIT_BRANCH_LOCAL) || (branch_type == GIT_BRANCH_REMOTE));

	if ((error = retrieve_branch_reference(&branch, repo, branch_name, branch_type == GIT_BRANCH_REMOTE)) < 0)
		return error;

	if (git_reference_lookup(&head, repo, GIT_HEAD_FILE) < 0) {
		giterr_set(GITERR_REFERENCE, "Cannot locate HEAD.");
		goto on_error;
	}

	if ((git_reference_type(head) == GIT_REF_SYMBOLIC)
		&& (strcmp(git_reference_target(head), git_reference_name(branch)) == 0)) {
			giterr_set(GITERR_REFERENCE,
					"Cannot delete branch '%s' as it is the current HEAD of the repository.", branch_name);
			goto on_error;
	}

	if (git_reference_delete(branch) < 0)
		goto on_error;

	git_reference_free(head);
	return 0;

on_error:
	git_reference_free(head);
	git_reference_free(branch);
	return -1;
}

typedef struct {
	int (*branch_cb)(
			const char *branch_name,
			git_branch_t branch_type,
			void *payload);
	void *callback_payload;
	unsigned int branch_type;
} branch_foreach_filter;

static int branch_foreach_cb(const char *branch_name, void *payload)
{
	branch_foreach_filter *filter = (branch_foreach_filter *)payload;

	if (filter->branch_type & GIT_BRANCH_LOCAL &&
		git__prefixcmp(branch_name, GIT_REFS_HEADS_DIR) == 0)
		return filter->branch_cb(branch_name + strlen(GIT_REFS_HEADS_DIR), GIT_BRANCH_LOCAL, filter->callback_payload);

	if (filter->branch_type & GIT_BRANCH_REMOTE &&
		git__prefixcmp(branch_name, GIT_REFS_REMOTES_DIR) == 0)
		return filter->branch_cb(branch_name + strlen(GIT_REFS_REMOTES_DIR), GIT_BRANCH_REMOTE, filter->callback_payload);

	return 0;
}

int git_branch_foreach(
		git_repository *repo,
		unsigned int list_flags,
		int (*branch_cb)(
			const char *branch_name,
			git_branch_t branch_type,
			void *payload),
		void *payload
)
{
	branch_foreach_filter filter;

	filter.branch_cb = branch_cb;
	filter.branch_type = list_flags;
	filter.callback_payload = payload;

	return git_reference_foreach(repo, GIT_REF_LISTALL, &branch_foreach_cb, (void *)&filter);
}

static int not_a_local_branch(git_reference *ref)
{
	giterr_set(GITERR_INVALID, "Reference '%s' is not a local branch.", git_reference_name(ref));
	return -1;
}

int git_branch_move(
	git_reference *branch,
	const char *new_branch_name,
	int force)
{
	git_buf new_reference_name = GIT_BUF_INIT;
	int error;

	assert(branch && new_branch_name);

	if (!git_reference_is_branch(branch))
		return not_a_local_branch(branch);

	if ((error = git_buf_joinpath(&new_reference_name, GIT_REFS_HEADS_DIR, new_branch_name)) < 0)
		goto cleanup;

	error = git_reference_rename(branch, git_buf_cstr(&new_reference_name), force);

cleanup:
	git_buf_free(&new_reference_name);

	return error;
}

int git_branch_lookup(
		git_reference **ref_out,
		git_repository *repo,
		const char *branch_name,
		git_branch_t branch_type)
{
	assert(ref_out && repo && branch_name);

	return retrieve_branch_reference(ref_out, repo, branch_name, branch_type == GIT_BRANCH_REMOTE);
}

static int retrieve_tracking_configuration(
	const char **out, git_reference *branch, const char *format)
{
	git_config *config;
	git_buf buf = GIT_BUF_INIT;
	int error;

	if (git_repository_config__weakptr(&config, git_reference_owner(branch)) < 0)
		return -1;

	if (git_buf_printf(&buf, format,
		git_reference_name(branch) + strlen(GIT_REFS_HEADS_DIR)) < 0)
			return -1;

	error = git_config_get_string(out, config, git_buf_cstr(&buf));
	git_buf_free(&buf);
	return error;
}

int git_branch_tracking(
		git_reference **tracking_out,
		git_reference *branch)
{
	const char *remote_name, *merge_name;
	git_buf buf = GIT_BUF_INIT;
	int error = -1;
	git_remote *remote = NULL;
	const git_refspec *refspec;

	assert(tracking_out && branch);

	if (!git_reference_is_branch(branch))
		return not_a_local_branch(branch);

	if ((error = retrieve_tracking_configuration(&remote_name, branch, "branch.%s.remote")) < 0)
		goto cleanup;

	if ((error = retrieve_tracking_configuration(&merge_name, branch, "branch.%s.merge")) < 0)
		goto cleanup;

	if (strcmp(".", remote_name) != 0) {
		if ((error = git_remote_load(&remote, git_reference_owner(branch), remote_name)) < 0)
			goto cleanup;

		refspec = git_remote_fetchspec(remote);
		if (refspec == NULL) {
			error = GIT_ENOTFOUND;
			goto cleanup;
		}

		if (git_refspec_transform_r(&buf, refspec, merge_name) < 0)
			goto cleanup;
	} else
		if (git_buf_sets(&buf, merge_name) < 0)
			goto cleanup;

	error = git_reference_lookup(
		tracking_out,
		git_reference_owner(branch),
		git_buf_cstr(&buf));

cleanup:
	git_remote_free(remote);
	git_buf_free(&buf);
	return error;
}
