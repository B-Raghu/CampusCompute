/* ipc.c - inter-process communication with POSIX shared memory and named semaphores
 *
 * Objects this program creates (and the only ones it ever removes):
 *   shared memory  /cc_shm
 *   semaphores     /cc_sem_data, /cc_sem_ack  (and /cc_sem_mutex in a later step)
 */
#include "campuscompute.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHM_NAME   "/cc_shm"
#define SEM_DATA   "/cc_sem_data"
#define SEM_ACK    "/cc_sem_ack"
#define SEM_MUTEX  "/cc_sem_mutex"
#define WAIT_TIMEOUT_SEC 5

/* The "notice board" both processes can read and write */
typedef struct {
    int  seq;           /* message number */
    int  cmd;           /* 0 = job message, 1 = quit */
    int  value;         /* CPU burst sent to the consumer */
    int  reply;         /* answer written back by the consumer */
    char text[32];      /* job name */
} Exchange;

/* ---------- helpers ---------- */

/* Create a fresh shared-memory object of 'size' bytes and map it into memory */
static void *shm_create(size_t size)
{
    shm_unlink(SHM_NAME);               /* remove a leftover of OUR name from a crashed run */
    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return NULL; }
    if (ftruncate(fd, (off_t)size) != 0) {
        perror("ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
        return NULL;
    }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        perror("mmap");
        shm_unlink(SHM_NAME);
        return NULL;
    }
    memset(p, 0, size);
    return p;
}

static void shm_destroy(void *p, size_t size)
{
    if (p) munmap(p, size);
    if (shm_unlink(SHM_NAME) == 0) printf("  cleanup: shared memory %s removed\n", SHM_NAME);
    else perror("shm_unlink");
}

static sem_t *sem_make(const char *name, unsigned initial)
{
    sem_unlink(name);                   /* leftover of OUR name, if any */
    sem_t *s = sem_open(name, O_CREAT | O_EXCL, 0600, initial);
    if (s == SEM_FAILED) { perror("sem_open"); return NULL; }
    return s;
}

static void sem_remove(sem_t *s, const char *name)
{
    if (s) sem_close(s);
    if (sem_unlink(name) == 0) printf("  cleanup: semaphore %s removed\n", name);
    else perror("sem_unlink");
}

/* Wait for a semaphore, but give up after a few seconds so we can never hang forever */
static int sem_wait_timeout(sem_t *s, int secs)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += secs;
    while (sem_timedwait(s, &ts) != 0) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

/* ---------- the exchange: parent = producer, child = consumer ---------- */

int cmd_ipc(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'ipc' takes no arguments\n");
        return 1;
    }

    static const int bursts[] = { 5, 3, 8, 6 };
    const int count = (int)(sizeof bursts / sizeof bursts[0]);
    int rc = 0;

    Exchange *ex = shm_create(sizeof *ex);
    if (!ex) return 1;
    sem_t *sd = sem_make(SEM_DATA, 0);                 /* "data is ready" signal */
    sem_t *sa = sd ? sem_make(SEM_ACK, 0) : NULL;      /* "reply is ready" signal */
    if (!sd || !sa) {
        if (sd) sem_remove(sd, SEM_DATA);
        shm_destroy(ex, sizeof *ex);
        return 1;
    }
    printf("Created shared memory %s (%zu bytes) and named semaphores %s, %s\n",
           SHM_NAME, sizeof *ex, SEM_DATA, SEM_ACK);
    fflush(stdout);

    pid_t pid = fork();                                /* now there are TWO processes */
    if (pid < 0) {
        perror("fork");
        sem_remove(sd, SEM_DATA);
        sem_remove(sa, SEM_ACK);
        shm_destroy(ex, sizeof *ex);
        return 1;
    }

    if (pid == 0) {                                    /* ---- consumer (child) ---- */
        for (;;) {
            if (sem_wait_timeout(sd, WAIT_TIMEOUT_SEC) != 0) {   /* wait: data ready? */
                fprintf(stderr, "  [consumer] timed out waiting for data\n");
                _exit(2);
            }
            if (ex->cmd == 1) break;                   /* quit message */
            printf("  [consumer pid %d] read from shared memory: seq=%d job='%s' burst=%d\n",
                   (int)getpid(), ex->seq, ex->text, ex->value);
            ex->reply = ex->value * 10;                /* pretend cost estimate */
            fflush(stdout);
            sem_post(sa);                              /* tell the producer: reply ready */
        }
        printf("  [consumer pid %d] quit message received, exiting\n", (int)getpid());
        fflush(stdout);
        munmap(ex, sizeof *ex);
        sem_close(sd);
        sem_close(sa);
        _exit(0);
    }

    /* ---- producer (parent) ---- */
    for (int i = 0; i < count; i++) {
        ex->seq = i + 1;
        ex->cmd = 0;
        ex->value = bursts[i];
        snprintf(ex->text, sizeof ex->text, "Job-%d", i + 1);
        printf("  [producer pid %d] wrote to shared memory: seq=%d job='%s' burst=%d\n",
               (int)getpid(), ex->seq, ex->text, ex->value);
        fflush(stdout);
        sem_post(sd);                                  /* tell the consumer: data ready */
        if (sem_wait_timeout(sa, WAIT_TIMEOUT_SEC) != 0) {   /* wait for the reply */
            fprintf(stderr, "  [producer] timed out waiting for consumer reply\n");
            rc = 1;
            break;
        }
        printf("  [producer pid %d] reply from consumer: estimated cost=%d\n",
               (int)getpid(), ex->reply);
        fflush(stdout);
    }
    ex->cmd = 1;                                       /* ask the consumer to quit */
    sem_post(sd);

    int status;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "  consumer did not exit cleanly\n");
        rc = 1;
    }

    sem_remove(sd, SEM_DATA);                          /* clean up everything we created */
    sem_remove(sa, SEM_ACK);
    shm_destroy(ex, sizeof *ex);
    printf("Shared-memory exchange %s.\n", rc == 0 ? "completed successfully" : "FAILED");
    log_write("IPC", "shared-memory exchange of %d messages %s", count,
              rc == 0 ? "completed" : "FAILED");
    return rc;
}

/* ---------- a semaphore used as a lock between two processes ---------- */

#define COUNTER_LOOPS 20000

typedef struct { volatile long counter; } SharedCounter;

int cmd_ipc_sem(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'ipc-sem' takes no arguments\n");
        return 1;
    }

    SharedCounter *sc = shm_create(sizeof *sc);
    if (!sc) return 1;
    sem_t *mx = sem_make(SEM_MUTEX, 1);          /* start value 1 = "lock is free" */
    if (!mx) {
        shm_destroy(sc, sizeof *sc);
        return 1;
    }
    printf("Created shared counter in %s and binary semaphore %s (start value 1)\n",
           SHM_NAME, SEM_MUTEX);
    printf("Parent and child each add 1 to the counter %d times; the semaphore guards every update.\n",
           COUNTER_LOOPS);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        sem_remove(mx, SEM_MUTEX);
        shm_destroy(sc, sizeof *sc);
        return 1;
    }
    if (pid == 0) {                              /* child */
        for (int i = 0; i < COUNTER_LOOPS; i++) {
            sem_wait(mx);                        /* take the lock */
            sc->counter++;                       /* only one process at a time gets here */
            sem_post(mx);                        /* give the lock back */
        }
        printf("  [child  pid %d] finished %d guarded updates\n", (int)getpid(), COUNTER_LOOPS);
        fflush(stdout);
        munmap(sc, sizeof *sc);
        sem_close(mx);
        _exit(0);
    }
    for (int i = 0; i < COUNTER_LOOPS; i++) {    /* parent does the same */
        sem_wait(mx);
        sc->counter++;
        sem_post(mx);
    }
    printf("  [parent pid %d] finished %d guarded updates\n", (int)getpid(), COUNTER_LOOPS);

    int status, rc = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) rc = 1;

    long expected = 2L * COUNTER_LOOPS;
    long final_value = sc->counter;              /* read BEFORE the memory is removed */
    printf("Final counter = %ld (expected %ld) -> %s\n", final_value, expected,
           final_value == expected ? "CORRECT, no lost updates" : "WRONG");
    if (final_value != expected) rc = 1;

    sem_remove(mx, SEM_MUTEX);
    shm_destroy(sc, sizeof *sc);
    log_write("IPC", "semaphore-guarded counter = %ld (expected %ld) %s", final_value, expected,
              rc == 0 ? "OK" : "FAILED");
    return rc;
}

/* ---------- cleanup verification and manual cleanup ---------- */

static int shm_exists(const char *name)
{
    int fd = shm_open(name, O_RDONLY, 0);        /* no O_CREAT: this never creates anything */
    if (fd >= 0) { close(fd); return 1; }
    return 0;
}

static int sem_exists(const char *name)
{
    sem_t *s = sem_open(name, 0);                /* no O_CREAT: this never creates anything */
    if (s != SEM_FAILED) { sem_close(s); return 1; }
    return 0;
}

int cmd_check(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'check' takes no arguments\n");
        return 1;
    }
    int leftovers = 0;
    const char *sems[] = { SEM_DATA, SEM_ACK, SEM_MUTEX };

    printf("Cleanup verification (only objects created by CampusCompute are checked):\n");
    if (shm_exists(SHM_NAME)) { printf("  shared memory %-14s : STILL EXISTS\n", SHM_NAME); leftovers++; }
    else printf("  shared memory %-14s : released\n", SHM_NAME);
    for (int i = 0; i < 3; i++) {
        if (sem_exists(sems[i])) { printf("  semaphore     %-14s : STILL EXISTS\n", sems[i]); leftovers++; }
        else printf("  semaphore     %-14s : released\n", sems[i]);
    }

    errno = 0;                                   /* any child process left behind? */
    pid_t r = waitpid(-1, NULL, WNOHANG);
    if (r == -1 && errno == ECHILD) printf("  child processes               : none remaining\n");
    else { printf("  child processes               : still present\n"); leftovers++; }

    FILE *fp = fopen("/proc/self/status", "r");  /* how many threads is this program running? */
    if (fp) {
        char line[128];
        while (fgets(line, sizeof line, fp)) {
            int t;
            if (sscanf(line, "Threads: %d", &t) == 1) {
                printf("  threads in this process       : %d %s\n", t,
                       t == 1 ? "(main thread only)" : "(extra threads alive!)");
                if (t != 1) leftovers++;
            }
        }
        fclose(fp);
    }

    printf("Result: %s\n", leftovers == 0
           ? "CLEAN - no program-created IPC objects, processes or threads remain"
           : "NOT CLEAN - run './campuscompute cleanup'");
    log_write("CHECK", "cleanup verification: %s", leftovers == 0 ? "clean" : "leftovers found");
    return leftovers == 0 ? 0 : 1;
}

/* Removes ONLY the objects named at the top of this file (useful after a crash) */
int cmd_cleanup(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'cleanup' takes no arguments\n");
        return 1;
    }
    const char *sems[] = { SEM_DATA, SEM_ACK, SEM_MUTEX };
    printf("Removing CampusCompute IPC objects (if present):\n");
    if (shm_unlink(SHM_NAME) == 0) printf("  removed shared memory %s\n", SHM_NAME);
    else printf("  shared memory %s: nothing to remove\n", SHM_NAME);
    for (int i = 0; i < 3; i++) {
        if (sem_unlink(sems[i]) == 0) printf("  removed semaphore %s\n", sems[i]);
        else printf("  semaphore %s: nothing to remove\n", sems[i]);
    }
    return 0;
}
