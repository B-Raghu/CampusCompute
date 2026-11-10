/* worker.c - worker processes and failure detection with fork() and waitpid()
 *
 * The parent creates child "workers" with fork() and collects each child's
 * result with waitpid(). The failures here are SIMULATED on purpose:
 *   - one worker exits with code 3        (simulated failure, controlled exit)
 *   - one worker sends SIGTERM to itself  (simulated abnormal termination)
 * They are NOT real operating-system faults.
 */
#include "campuscompute.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIM_EXIT_CODE 3
#define MAX_WORKERS   8

typedef enum { W_NORMAL, W_EXIT_FAIL, W_SIGNAL_FAIL } WorkerMode;

/* Turns a wait status into readable text (also used by limits.c later) */
void describe_status(int status, char *buf, size_t len)
{
    if (WIFEXITED(status))
        snprintf(buf, len, "exited normally with code %d", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        snprintf(buf, len, "killed by signal %d (%s)", WTERMSIG(status),
                 strsignal(WTERMSIG(status)));
    else
        snprintf(buf, len, "unknown status 0x%x", status);
}

/* The code that runs inside the child process. It never returns. */
static void worker_body(int id, WorkerMode mode)
{
    printf("  [worker %d pid %d] started\n", id, (int)getpid());
    volatile unsigned long sum = 0;
    for (unsigned long i = 0; i < 5000000UL; i++) sum += i;     /* a little "work" */

    switch (mode) {
    case W_NORMAL:
        printf("  [worker %d pid %d] task finished normally (result=%lu)\n",
               id, (int)getpid(), (unsigned long)sum);
        fflush(stdout);
        _exit(0);
    case W_EXIT_FAIL:
        printf("  [worker %d pid %d] *** SIMULATED FAILURE: exiting with code %d (not a real OS fault) ***\n",
               id, (int)getpid(), SIM_EXIT_CODE);
        fflush(stdout);
        _exit(SIM_EXIT_CODE);
    case W_SIGNAL_FAIL:
        printf("  [worker %d pid %d] *** SIMULATED FAILURE: sending SIGTERM to itself (not a real OS fault) ***\n",
               id, (int)getpid());
        fflush(stdout);
        raise(SIGTERM);
        _exit(4);                                 /* only reached if SIGTERM were ignored */
    }
    _exit(0);
}

/* Starts all workers, waits for each one, and prints a verdict per worker.
 * Returns the number of workers detected as failed, or -1 on a system error. */
static int run_workers(const WorkerMode modes[], int count)
{
    pid_t pids[MAX_WORKERS];
    int started = 0, failed = 0;

    fflush(stdout);                               /* avoid duplicated buffered output in children */
    for (int i = 0; i < count; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            break;
        }
        if (pid == 0) worker_body(i + 1, modes[i]);      /* child: never returns */
        pids[started++] = pid;                           /* parent: remember the child */
        printf("  [parent] created worker %d with pid %d\n", i + 1, (int)pid);
        fflush(stdout);
    }

    for (int i = 0; i < started; i++) {
        int status;
        pid_t r;
        do { r = waitpid(pids[i], &status, 0); } while (r < 0 && errno == EINTR);
        if (r < 0) {
            perror("waitpid");
            return -1;
        }
        char desc[96];
        describe_status(status, desc, sizeof desc);
        int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        printf("  [parent] worker %d (pid %d) %s -> %s\n", i + 1, (int)pids[i], desc,
               ok ? "SUCCESS" : "FAILURE DETECTED");
        log_write("WORKER", "worker %d pid %d %s -> %s", i + 1, (int)pids[i], desc,
                  ok ? "success" : "FAILURE");
        if (!ok) failed++;
    }
    if (started != count) return -1;
    return failed;
}

int cmd_worker_normal(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'worker-normal' takes no arguments\n");
        return 1;
    }
    static const WorkerMode modes[] = { W_NORMAL, W_NORMAL, W_NORMAL };
    printf("Creating 3 normal workers with fork() and collecting status with waitpid():\n");
    int failed = run_workers(modes, 3);
    if (failed != 0) {
        printf("Result: unexpected failures detected (%d)\n", failed);
        return 1;
    }
    printf("Result: all 3 workers completed normally (exit code 0).\n");
    return 0;
}

int cmd_worker_fail(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'worker-fail' takes no arguments\n");
        return 1;
    }
    static const WorkerMode modes[] = { W_NORMAL, W_EXIT_FAIL, W_SIGNAL_FAIL };
    printf("Creating 3 workers: 1 normal and 2 with SIMULATED failures (clearly marked below):\n");
    int failed = run_workers(modes, 3);
    if (failed != 2) {
        printf("Result: expected 2 simulated failures but detected %d\n", failed);
        return 1;
    }
    printf("Result: parent detected exactly 2 simulated failures (exit code %d and SIGTERM) "
           "and 1 normal completion.\n", SIM_EXIT_CODE);
    return 0;
}
