# clone-gitlab-api

Clone locally all git projects from Gitlab. This is handy for example to search locally with `ripgrep` very quickly, or hack on projects.

Every project is cloned concurrently for maximum performance and the memory usage of the main process remains **under 4 MiB** even with thousands of big projects (without counting git's memory usage, of course), and the single executable is ~70 KiB.

## Usage

*The api token is optional. Without it, only publicly accessible repositories can be cloned. Go to https://gitlab.example.com/-/profile/personal_access_tokens to create one with `read_api` and `read_repository` access.*

Usage:

```
Clone or update all git repositories from Gitlab.

USAGE:
	clone-gitlab-api [OPTIONS]

OPTIONS:
	-m, --clone-method=https|ssh        Clone over https or ssh. Defaults to ssh.
	-d, --root-directory <DIRECTORY>    The root directory to clone/update all the projects. Required.
	-u, --url <GITLAB URL>
	-t, --api-token <API TOKEN>         The api token from gitlab to fetch private repositories
	-h, --help
	-v, --verbose

The repositories are cloned with git over ssh with, without tags, in a flat manner.
If some repositories already exist in the root directory, they are updated (with git pull) instead of cloned.
If some repositories fail, this command does not stop and tries to clone or update the other repositories.

EXAMPLES:

Clone/update all repositories from gitlab.com over https in the directory /tmp/git:

	clone-gitlab-api -u gitlab.com -d /tmp/git/ --clone-method=https

Clone/update all repositories from gitlab.example.com (over ssh which is the default) with the token 'abcdef123' in the directory /tmp/git verbosely:

	clone-gitlab-api -u gitlab.example.com -t abcdef123 -d /tmp/git/ -v

```

Install (make sure first that git submodules have been pulled with e.g. `git submodule update --init --recursive`):

```sh
$ brew install curl git
$ make install
```

Example (the exact output will be different for you):

```
$ clone-gitlab-api -u gitlab.example.com -d /tmp/git
Changed directory to: /tmp/git
[1/13] ✓ foo/bar
...
[13/13] ✓ hello/world
Finished in 2s

$ ls /tmp/git/
foo.bar
...
hello.world
```

## Limitations

- We use `kqueue` internally so it's MacOS/BSDs only for now. It should work on Linux by installing `libkqueue` but this is untested.

## Roadmap

- [ ] Retrying HTTP requests
- [ ] `--me` option to only clone my repositories
- [ ] Max network rate CLI option
- [ ] Git clone/pull options
- [ ] Linux support


## Implementation

This command is essentially a small git driver. It will fetch the list of projects from Gitlab's API, and for each project, issue a git command in a child process. If the directory already exists, it will be `git pull`, otherwise `git clone`.

A background thread waits for those child processes to finish and prints whether the corresponding project succeeded or not. The stderr output of the child processes is captured and printed alongside.

We make sure we use the least amount of memory and don't do anything we don't absolutely have to do (e.g. parsing all of the JSON fields returned by Gitlab's API when we only are interested in two of them).
