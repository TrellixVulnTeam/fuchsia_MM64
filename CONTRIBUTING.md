Contributing Changes
====================

Fuchsia manages commits through Gerrit at
https://fuchsia-review.googlesource.com. Not all projects accept patches;
please see the CONTRIBUTING.md document in individual projects for
details.

## Submitting changes

To submit a patch to Fuchsia, you may first need to generate a cookie to
authenticate you to Gerrit. To generate a cookie, log into Gerrit and click
the "Generate Password" link at the top of https://fuchsia.googlesource.com.
Then, copy the generated text and execute it in a terminal.

Once authenticated, follow these steps to submit a patch to a repo in Fuchsia:

```
# create a new branch
git checkout -b branch_name

# write some awesome stuff, commit to branch_name
# edit some_file ...
git add some_file
# if specified in the repo, follow the commit message format
git commit ...

# upload the patch to Gerrit
# `jiri help upload` lists flags for various features, e.g. adding reviewers
jiri upload # Adds default topic - ${USER}-branch_name
# or
jiri upload -topic="custom_topic"
# or
git push origin HEAD:refs/for/master

# at any time, if you'd like to make changes to your patch, use --amend
git commit --amend

# once the change is landed, clean up the branch
git branch -d branch_name
```

See the Gerrit documentation for more detail:
[https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change](https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change)

### Commit message tags

If submitting a change to Zircon, Garnet, Peridot or Topaz, include [tags] in
the commit subject flagging which module, library, app, etc, is affected by the
change. The style here is somewhat informal. Look at these example changes to
get a feel for how these are used.

* https://fuchsia-review.googlesource.com/c/zircon/+/112976
* https://fuchsia-review.googlesource.com/c/garnet/+/110795
* https://fuchsia-review.googlesource.com/c/peridot/+/113955
* https://fuchsia-review.googlesource.com/c/topaz/+/114013

Gerrit will flag your change with
`Needs Label: Commit-Message-has-tags` if these are missing.

Example:
```
# Ready to submit
[parent][component] Update component in Topaz.
Test: Added test X

# Needs Label: Commit-Message-has-tags
Update component in Topaz.
Test: Added test X
```

## Testing

Developers are responsible for high-quality automated testing of their code.
Reviewers are responsible for pushing back on changes that do not include
sufficient tests.

If a change requires non-obvious manual testing for validation, those testing
steps should be described in a line in the change description beginning with
"Test:". If the instructions are more elaborate, they can be added to a linked
bug.

In some cases, we are not able to test certain behavior changes because we lack
some particular piece of infrastructure. In that case, we should have an issue
in the tracker about creating that infrastructure and the test label should
mention the bug number in addition to describing how the change was manually
tested:

```
Test: Manually tested that [...]. Automated testing needs US-XXXX
```

If the change does not intend to change behavior, the CL description should
indicate as such.

## [Non-Googlers only] Sign the Google CLA

In order to land your change, you need to sign the [Google CLA](https://cla.developers.google.com/).

## [Googlers only] Issue actions

Commit messages may reference issue IDs in Fuchsia's
[issue tracker](https://fuchsia.atlassian.net/); such references will become
links in the Gerrit UI. Issue actions may also be specified, for example to
automatically close an issue when a commit is landed:

BUG-123 #done

`done` is the most common issue action, though any workflow action can be
indicated in this way.

Issue actions take place when the relevant commit becomes visible in a Gerrit
branch, with the exception that commits under refs/changes/ are ignored.
Usually, this means the action will happen when the commit is merged to
master, but note that it will also happen if a change is uploaded to a private
branch.

*Note*: Fuchsia's issue tracker is not open to external contributors at this
time.

## Cross-repo changes

Changes in two or more separate repos will be automatically tracked for you by
Gerrit if you use the same topic.

### Using jiri upload
Create branch with same name on all repos and upload the changes
```
# make and commit the first change
cd examples/fortune
git checkout -b add_feature_foo
* edit foo_related_files ... *
git add foo_related_files ...
git commit ...

# make and commit the second change in another repository
cd fuchsia/build
git checkout -b add_feature_foo
* edit more_foo_related_files ... *
git add more_foo_related_files ...
git commit ...

# Upload all changes with the same branch name across repos
jiri upload -multipart # Adds default topic - ${USER}-branch_name
# or
jiri upload -multipart -topic="custom_topic"

# after the changes are reviewed, approved and submitted, clean up the local branch
cd examples/fortune
git branch -d add_feature_foo

cd fuchsia/build
git branch -d add_feature_foo
```

### Using Gerrit commands

```
# make and commit the first change, upload it with topic 'add_feature_foo'
cd examples/fortune
git checkout -b add_feature_foo
* edit foo_related_files ... *
git add foo_related_files ...
git commit ...
git push origin HEAD:refs/for/master%topic=add_feature_foo

# make and commit the second change in another repository
cd fuchsia/build
git checkout -b add_feature_foo
* edit more_foo_related_files ... *
git add more_foo_related_files ...
git commit ...
git push origin HEAD:refs/for/master%topic=add_feature_foo

# after the changes are reviewed, approved and submitted, clean up the local branch
cd examples/fortune
git branch -d add_feature_foo

cd fuchsia/build
git branch -d add_feature_foo
```

Multipart changes are tracked in Gerrit via topics, will be tested together,
and can be landed in Gerrit at the same time with `Submit Whole Topic`. Topics
can be edited via the web UI.

## Changes that span repositories

See [Changes that span repositories](development/workflows/multilayer_changes.md).

## Resolving merge conflicts

```
# rebase from origin/master, revealing the merge conflict
git rebase origin/master

# resolve the conflicts and complete the rebase
* edit files_with_conflicts ... *
git add files_with_resolved_conflicts ...
git rebase --continue
jiri upload

# continue as usual
git commit --amend
jiri upload
```
