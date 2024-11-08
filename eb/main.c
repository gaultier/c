#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../pg/pg.h"

typedef struct {
  uint64_t max_retries, max_duration_seconds;
  uint32_t wait_milliseconds;
  PG_PAD(4);
} options_t;

static void print_usage(char *argv0) {
  printf(
      // clang-format off
        "%s <command> [arguments...]\n"
        "Options:\n"
        "    --max-retries, -r             How many maximum retries to attempt.\n"
        "    --max-duration-seconds, -d    How long to run.\n"
        "    --wait-ms, -w                 How much time to wait between invocations of the command.\n"
        "    --help, -h                    This help message.\n"
        "Examples:\n"
        "    %s curl google.com\n"
        "    %s --max-duration-seconds 5 -- nc -z google.com 80\n"
        "    %s -r 5 -- make test\n"
        "    %s -w 100 -- echo $PWD\n"
        "    %s --help\n",
      // clang-format on
      argv0, argv0, argv0, argv0, argv0, argv0);
}

static void options_parse_from_cli(int argc, char *argv[], options_t *options) {
  struct option longopts[] = {
      {.name = "max-retries",
       .has_arg = required_argument,
       .flag = NULL,
       .val = 'r'},
      {.name = "max-duration-seconds",
       .has_arg = required_argument,
       .flag = NULL,
       .val = 'd'},
      {.name = "wait-ms",
       .has_arg = required_argument,
       .flag = NULL,
       .val = 'w'},
      {.name = "help", .has_arg = no_argument, .flag = NULL, .val = 'h'},
  };

  int ch = 0;
  while ((ch = getopt_long(argc, argv, "hr:d:w:", longopts, NULL)) != -1) {
    switch (ch) {
    case 'r': {
      options->max_retries = strtoull(optarg, NULL, 10);
      break;
    }
    case 'd': {
      options->max_duration_seconds = strtoull(optarg, NULL, 10);
      break;
    }
    case 'w': {
      options->wait_milliseconds = (uint32_t)strtoul(optarg, NULL, 10);
      break;
    }
    default:
      print_usage(argv[0]);
      exit(0);
    }
  }
}

static void *exit_after_max_duration(void *arg) {
  uint32_t *max_duration_seconds = arg;
  sleep(*max_duration_seconds);
  exit(EINTR);
  return NULL;
}

int main(int argc, char *argv[], char *envp[]) {
  signal(SIGUSR1, NULL);
  if (argc == 1) {
    return EINVAL;
  }
  options_t options = {.max_retries = UINT64_MAX};
  options_parse_from_cli(argc, argv, &options);
  argc -= optind;
  argv += optind;

  if (options.max_duration_seconds > 0) {
    pthread_t thread = {0};
    pthread_create(&thread, NULL, exit_after_max_duration,
                   &options.max_duration_seconds);
  }

  uint32_t sleep_milliseconds = 100;
  for (uint64_t attempt = 0; attempt < options.max_retries; attempt++) {
    for (int i = 0; i < argc; i++) {
      printf("%s ", argv[i]);
    }
    printf("\n");

    pid_t pid = {0};
    int ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, envp);
    if (ret < 0) {
      fprintf(stderr, "Failed to run command (malformed?): %s",
              strerror(errno));
      return errno;
    }

    int status = 0;
    if (wait(&status) == -1) {
      fprintf(stderr, "Failed wait(2): %s", strerror(errno));
      return errno;
    }
    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status) == 0)
        return 0;

      usleep(1000 * (options.wait_milliseconds > 0 ? options.wait_milliseconds
                                                   : sleep_milliseconds));
      sleep_milliseconds =
          (uint32_t)(sleep_milliseconds * 1.5); // TODO: jitter, random
      continue;
    }
    // TODO
  }
}
