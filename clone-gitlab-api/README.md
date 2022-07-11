# clone-gitlab-api

Clone locally all git projects from Gitlab. This is handy for example to search locally with `ripgrep` very quickly, or hack on projects.

Every project is cloned concurrently for maximum performance and the memory usage remains under 30MiB even with thousands of big projects (without counting git, of course).

## Usage

*The api token is optional. Without it, only publicly accessible repositories can be cloned. Go to https://gitlab.custom.com/-/profile/personal_access_tokens to create one with `read_api` and `read_repository` access.*

Usage:

```
Clone all git repositories from Gitlab.

USAGE:
	./clone-gitlab-api [OPTIONS]

OPTIONS:
	-d, --root-directory <DIRECTORY>    The root directory to clone all the projects
	-u, --url <GITLAB URL>
	-t, --api-token <API TOKEN>         The api token from gitlab to fetch private repositories
	-h, --help
	-v, --verbose

The repositories are cloned with git over ssh with a depth of 1, without tags, in a flat manner.
If some repositories already exist in the root directory, they are updated instead of cloned.
If some repositories fail, this command does not stop and tries to clone or update the other repositories.

EXAMPLES:

Clone all repositories from gitlab.com with the token 'abcdef123' in the directory /tmp/git:

	clone-gitlab-api -u gitlab.com -t abcdef123 -d /tmp/git/

Clone all repositories from gitlab.custom.com with the token 'abcdef123' in the directory /tmp/git verbosely:

	clone-gitlab-api -u gitlab.custom.com -t abcdef123 -d /tmp/git/ -v

```

Build:

```sh
$ make
```

Example (the exact output will be different for you):

```sh
$ clone-gitlab-api -u gitlab.custom.com -d /tmp/git
Changed directory to: /tmp/git
[1/13] ✓ foo/bar
...
[13/13] ✓ hello/world
Finished in 2s
```

## Development

```sh
# Adapt for your platform
$ brew install curl
$ make
```

## Roadmap

- [ ] Retrying
- [ ] `--me` option to only clone my repositories
- [ ] Stop if no project could be fetched from the Gitlab API at all
- [ ] Max network rate CLI option
- [ ] Git clone/pull options
