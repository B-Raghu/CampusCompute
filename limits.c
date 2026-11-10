/* limits.c - process resource limits with setrlimit()
 *
 * Each test runs in its OWN child process, so a limit never affects the
 * parent or the rest of the program. Three limits are demonstrated:
 *   RLIMIT_NOFILE : open() starts failing with EMFILE
 *   RLIMIT_AS     : a large malloc() fails with ENOMEM
 *   RLIMIT_CPU    : the kernel sends SIGXCPU and the process is terminated
 */
#include "campuscompute.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define FD_LIMIT   16
#define MEM_LIMIT  (256UL * 1024 * 1024)

/* Test 1: limit how many files a process may have open */
static void limit_open_files(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) { perror("getrlimit"); _exit(1); }
    printf("  before: RLIMIT_NOFILE soft=%lu hard=%lu\n",
           (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max);
    if (rl.rlim_cur > FD_LIMIT) rl.rlim_cur = FD_LIMIT;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) { perror("setrlimit"); _exit(1); }
    printf("  applied: soft limit lowered to %lu open files\n", (unsigned long)rl.rlim_cur);

    int fds[64], count = 0;
    while (count < 64) {                         /* keep opening files until it fails */
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            printf("  effect : open() failed after %d extra files: %s (errno %d)\n",
                   count, strerror(errno), errno);
            break;
        }
        fds[count++] = fd;
    }
    if (count == 64) printf("  effect : no failure observed (unexpected)\n");
    for (int i = 0; i < count; i++) close(fds[i]);
    if (count == 64) _exit(1);
}

/* Test 2: limit how much memory (address space) a process may use */
static void limit_memory(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_AS, &rl) != 0) { perror("getrlimit"); _exit(1); }
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < MEM_LIMIT) rl.rlim_cur = rl.rlim_max;
    else rl.rlim_cur = MEM_LIMIT;
    if (setrlimit(RLIMIT_AS, &rl) != 0) { perror("setrlimit"); _exit(1); }
    printf("  applied: RLIMIT_AS (address space) = %lu MB\n",
           (unsigned long)(rl.rlim_cur / (1024 * 1024)));

    char *volatile small = malloc(16UL * 1024 * 1024);
    printf("  malloc(16 MB)  : %s\n", small ? "succeeded (below the limit)" : "FAILED");
    if (small) { small[0] = 1; free(small); }

    char *volatile big = malloc(512UL * 1024 * 1024);
    if (big) {
        big[0] = 1;
        printf("  effect : malloc(512 MB) succeeded (unexpected)\n");
        free(big);
        _exit(1);
    }
    printf("  effect : malloc(512 MB) FAILED: %s (errno %d)\n", strerror(errno), errno);
}

/* Test 3: limit CPU time. The loop below never ends, so the kernel stops it. */
static void limit_cpu(void)
{
    struct rlimit core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &core);               /* no core-dump file when the kernel kills us */
    struct rlimit rl = { .rlim_cur = 1, .rlim_max = 2 };
    if (setrlimit(RLIMIT_CPU, &rl) != 0) { perror("setrlimit"); _exit(1); }
    printf("  applied: RLIMIT_CPU soft=1s hard=2s; now spinning in an endless loop...\n");
    fflush(stdout);
    volatile unsigned long x = 0;
    for (;;) x++;
}

/* Runs fn() in a child process. expect_signal == 0 means "must exit with code 0". */
static int run_in_child(const char *title, void (*fn)(void), int expect_signal)
{
    printf("\n--- %s ---\n", title);
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        fn();
        fflush(stdout);
        _exit(0);
    }
    int status;
    pid_t r;
    do { r = waitpid(pid, &status, 0); } while (r < 0 && errno == EINTR);
    if (r < 0) { perror("waitpid"); return 1; }

    char desc[96];
    describe_status(status, desc, sizeof desc);
    int ok = expect_signal ? (WIFSIGNALED(status) && WTERMSIG(status) == expect_signal)
                           : (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    printf("  [parent] child pid %d %s -> %s\n", (int)pid, desc, ok ? "as expected" : "UNEXPECTED");
    log_write("LIMITS", "%s: child %s -> %s", title, desc, ok ? "as expected" : "UNEXPECTED");
    if (ok && expect_signal == SIGXCPU)
        printf("  explanation: the kernel sent SIGXCPU when the 1-second CPU-time limit was reached.\n");
    return ok ? 0 : 1;
}

int cmd_limits(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'limits' takes no arguments\n");
        return 1;
    }
    printf("Resource-limit demonstration with setrlimit() (each test runs in its own child process)\n");
    int rc = 0;
    rc |= run_in_child("Test 1: RLIMIT_NOFILE (max open files)", limit_open_files, 0);
    rc |= run_in_child("Test 2: RLIMIT_AS (max memory / address space)", limit_memory, 0);
    rc |= run_in_child("Test 3: RLIMIT_CPU (max CPU seconds)", limit_cpu, SIGXCPU);
    printf("\nResult: %s\n", rc == 0 ? "all three limits had the expected effect"
                                     : "some limits did not behave as expected");
    return rc;
}
