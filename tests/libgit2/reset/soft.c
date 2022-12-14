#include "clar_libgit2.h"
#include "posix.h"
#include "reset_helpers.h"
#include "path.h"
#include "repo/repo_helpers.h"

static git_repository *repo;
static git_object *target;

void test_reset_soft__initialize(void)
{
	repo = cl_git_sandbox_init("testrepo.git");
}

void test_reset_soft__cleanup(void)
{
	git_object_free(target);
	target = NULL;

	cl_git_sandbox_cleanup();
}

static void assert_reset_soft(bool should_be_detached)
{
	git_oid oid;

	cl_git_pass(git_reference_name_to_id(&oid, repo, "HEAD"));
	cl_git_fail(git_oid_streq(&oid, KNOWN_COMMIT_IN_BARE_REPO));
	cl_git_pass(git_revparse_single(&target, repo, KNOWN_COMMIT_IN_BARE_REPO));

	cl_assert(git_repository_head_detached(repo) == should_be_detached);

	cl_git_pass(git_reset(repo, target, GIT_RESET_SOFT, NULL));

	cl_assert(git_repository_head_detached(repo) == should_be_detached);

	cl_git_pass(git_reference_name_to_id(&oid, repo, "HEAD"));
	cl_git_pass(git_oid_streq(&oid, KNOWN_COMMIT_IN_BARE_REPO));
}

void test_reset_soft__can_reset_the_non_detached_Head_to_the_specified_commit(void)
{
	assert_reset_soft(false);
}

void test_reset_soft__can_reset_the_detached_Head_to_the_specified_commit(void)
{
	git_repository_detach_head(repo);

	assert_reset_soft(true);
}

void test_reset_soft__resetting_to_the_commit_pointed_at_by_the_Head_does_not_change_the_target_of_the_Head(void)
{
	git_oid oid;
	char raw_head_oid[GIT_OID_SHA1_HEXSIZE + 1];

	cl_git_pass(git_reference_name_to_id(&oid, repo, "HEAD"));
	git_oid_fmt(raw_head_oid, &oid);
	raw_head_oid[GIT_OID_SHA1_HEXSIZE] = '\0';

	cl_git_pass(git_revparse_single(&target, repo, raw_head_oid));

	cl_git_pass(git_reset(repo, target, GIT_RESET_SOFT, NULL));

	cl_git_pass(git_reference_name_to_id(&oid, repo, "HEAD"));
	cl_git_pass(git_oid_streq(&oid, raw_head_oid));
}

void test_reset_soft__resetting_to_a_tag_sets_the_Head_to_the_peeled_commit(void)
{
	git_oid oid;

	/* b25fa35 is a tag, pointing to another tag which points to commit e90810b */
	cl_git_pass(git_revparse_single(&target, repo, "b25fa35"));

	cl_git_pass(git_reset(repo, target, GIT_RESET_SOFT, NULL));

	cl_assert(git_repository_head_detached(repo) == false);
	cl_git_pass(git_reference_name_to_id(&oid, repo, "HEAD"));
	cl_git_pass(git_oid_streq(&oid, KNOWN_COMMIT_IN_BARE_REPO));
}

void test_reset_soft__cannot_reset_to_a_tag_not_pointing_at_a_commit(void)
{
	/* 53fc32d is the tree of commit e90810b */
	cl_git_pass(git_revparse_single(&target, repo, "53fc32d"));

	cl_git_fail(git_reset(repo, target, GIT_RESET_SOFT, NULL));
	git_object_free(target);

	/* 521d87c is an annotated tag pointing to a blob */
	cl_git_pass(git_revparse_single(&target, repo, "521d87c"));
	cl_git_fail(git_reset(repo, target, GIT_RESET_SOFT, NULL));
}

void test_reset_soft__resetting_against_an_unborn_head_repo_makes_the_head_no_longer_unborn(void)
{
	git_reference *head;

	cl_git_pass(git_revparse_single(&target, repo, KNOWN_COMMIT_IN_BARE_REPO));

	make_head_unborn(repo, NON_EXISTING_HEAD);

	cl_assert_equal_i(true, git_repository_head_unborn(repo));

	cl_git_pass(git_reset(repo, target, GIT_RESET_SOFT, NULL));

	cl_assert_equal_i(false, git_repository_head_unborn(repo));

	cl_git_pass(git_reference_lookup(&head, repo, NON_EXISTING_HEAD));
	cl_assert_equal_i(0, git_oid_streq(git_reference_target(head), KNOWN_COMMIT_IN_BARE_REPO));

	git_reference_free(head);
}

void test_reset_soft__fails_when_merging(void)
{
	git_str merge_head_path = GIT_STR_INIT;

	cl_git_pass(git_repository_detach_head(repo));
	cl_git_pass(git_str_joinpath(&merge_head_path, git_repository_path(repo), "MERGE_HEAD"));
	cl_git_mkfile(git_str_cstr(&merge_head_path), "beefbeefbeefbeefbeefbeefbeefbeefbeefbeef\n");

	cl_git_pass(git_revparse_single(&target, repo, KNOWN_COMMIT_IN_BARE_REPO));

	cl_assert_equal_i(GIT_EUNMERGED, git_reset(repo, target, GIT_RESET_SOFT, NULL));
	cl_git_pass(p_unlink(git_str_cstr(&merge_head_path)));

	git_str_dispose(&merge_head_path);
}

void test_reset_soft__fails_when_index_contains_conflicts_independently_of_MERGE_HEAD_file_existence(void)
{
	git_index *index;
	git_reference *head;
	git_str merge_head_path = GIT_STR_INIT;

	cl_git_sandbox_cleanup();

	repo = cl_git_sandbox_init("mergedrepo");

	cl_git_pass(git_str_joinpath(&merge_head_path, git_repository_path(repo), "MERGE_HEAD"));
	cl_git_pass(p_unlink(git_str_cstr(&merge_head_path)));
	git_str_dispose(&merge_head_path);

	cl_git_pass(git_repository_index(&index, repo));
	cl_assert_equal_i(true, git_index_has_conflicts(index));
	git_index_free(index);

	cl_git_pass(git_repository_head(&head, repo));
	cl_git_pass(git_reference_peel(&target, head, GIT_OBJECT_COMMIT));
	git_reference_free(head);

	cl_assert_equal_i(GIT_EUNMERGED, git_reset(repo, target, GIT_RESET_SOFT, NULL));
}

void test_reset_soft__reflog_is_correct(void)
{
	git_annotated_commit *annotated;
	const char *exp_msg = "checkout: moving from br2 to master";
	const char *master_msg = "commit: checking in";

	reflog_check(repo, "HEAD", 7, "yoram.harmelin@gmail.com", exp_msg);
	reflog_check(repo, "refs/heads/master", 2, "yoram.harmelin@gmail.com", master_msg);

	/* Branch not moving, no reflog entry */
	cl_git_pass(git_revparse_single(&target, repo, "HEAD^{commit}"));
	cl_git_pass(git_reset(repo, target, GIT_RESET_SOFT, NULL));
	reflog_check(repo, "HEAD", 7, "yoram.harmelin@gmail.com", exp_msg);
	reflog_check(repo, "refs/heads/master", 2, "yoram.harmelin@gmail.com", master_msg);
	git_object_free(target);

	/* Moved branch, expect id in message */
	exp_msg = "reset: moving to be3563ae3f795b2b4353bcce3a527ad0a4f7f644";
	cl_git_pass(git_revparse_single(&target, repo, "HEAD~^{commit}"));
	cl_git_pass(git_reset(repo, target, GIT_RESET_SOFT, NULL));
	reflog_check(repo, "HEAD", 8, "yoram.harmelin@gmail.com", exp_msg);
	reflog_check(repo, "refs/heads/master", 3, NULL, exp_msg);

	/* Moved branch, expect message with annotated string */
	exp_msg = "reset: moving to HEAD~^{commit}";
	cl_git_pass(git_annotated_commit_from_revspec(&annotated, repo, "HEAD~^{commit}"));
	cl_git_pass(git_reset_from_annotated(repo, annotated, GIT_RESET_SOFT, NULL));
	reflog_check(repo, "HEAD", 9, "yoram.harmelin@gmail.com", exp_msg);
	reflog_check(repo, "refs/heads/master", 4, NULL, exp_msg);

	git_annotated_commit_free(annotated);
}
