# clone-gitlab-api

Clone locally all git projects from Gitlab. This is handy for example to search locally with `ripgrep` very quickly, or hack on projects.

Every project is cloned concurrently for maximum performance and the memory usage of the main process remains **under 5 MiB** even with thousands of big projects (without counting git's memory usage, of course), and the single executable is smaller than 100 KiB.

## Usage

*The api token is optional. Without it, only publicly accessible repositories can be cloned. Go to https://gitlab.custom.com/-/profile/personal_access_tokens to create one with `read_api` and `read_repository` access.*

Usage:

```shell
$ clone-gitlab-api --help

Clone or update all git repositories from Gitlab.

USAGE:
	clone-gitlab-api [OPTIONS]

OPTIONS:
	-d, --root-directory <DIRECTORY>    The root directory to clone/update all the projects
	-u, --url <GITLAB URL>
	-t, --api-token <API TOKEN>         The api token from gitlab to fetch private repositories
	-h, --help
	-v, --verbose

The repositories are cloned with git over ssh with a depth of 1, without tags, in a flat manner.
If some repositories already exist in the root directory, they are updated (with git pull) instead of cloned.
If some repositories fail, this command does not stop and tries to clone or update the other repositories.

EXAMPLES:

	clone-gitlab-api -u gitlab.com -t abcdef123 -d /tmp/git/

Clone/update all repositories from gitlab.custom.com with the token 'abcdef123' in the directory /tmp/git verbosely:

	clone-gitlab-api -u gitlab.custom.com -t abcdef123 -d /tmp/git/ -v

```

Install:

```sh
$ brew install curl git
$ make install
```

Example (the exact output will be different for you):

```sh
$ clone-gitlab-api -u gitlab.custom.com -d /tmp/git
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

- Due to [Gitlab's pagination behavior](https://docs.gitlab.com/ee/api/index.html#pagination-response-headers) when there are lots of items, some HTTP headers which we rely upon are missing in the response, so the command will fail when there are more than 10,000 projects.
- We use `kqueue` internally so it's MacOS/BSDs only for now. It should work on Linux by installing `libkqueue` but this is untested.

## Roadmap

- [ ] Retrying
- [ ] `--me` option to only clone my repositories
- [ ] Max network rate CLI option
- [ ] Git clone/pull options
- [ ] Do not rely on HTTP headers which are sometimes missing
- [ ] Linux support
- [ ] Stderr of child process over pipe
