/* logger.c - concurrent activity logger with POSIX threads
 *
 * Producer threads put log messages into a bounded queue (room for 4).
 * One consumer thread takes them out and writes them to the log file.
 * A mutex protects the queue; two condition variables make threads wait
 * when the queue is full (producers) or empty (consumer).
 */
#include "campuscompute.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_CAP     4
#define MSG_LEN       96
#define MAX_PRODUCERS 10

/* ---------- part 1: writing lines to the log file (thread-safe) ---------- */

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void ensure_log_dir(void)
{
    if (mkdir(LOG_DIR, 0755) != 0 && errno != EEXIST) perror("mkdir logs");
}

/* Start a fresh, empty log file */
void log_reset(void)
{
    pthread_mutex_lock(&log_lock);
    ensure_log_dir();
    FILE *fp = fopen(LOG_PATH, "w");
    if (fp) fclose(fp);
    else perror("log_reset: fopen");
    pthread_mutex_unlock(&log_lock);
}

/* Add one timestamped line: [time] [MODULE] message */
void log_write(const char *module, const char *fmt, ...)
{
    char msg[256], ts[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);

    pthread_mutex_lock(&log_lock);               /* one writer at a time */
    ensure_log_dir();
    FILE *fp = fopen(LOG_PATH, "a");
    if (fp) {
        fprintf(fp, "[%s] [%-6s] %s\n", ts, module, msg);
        fclose(fp);
    } else {
        perror("log_write: fopen");
    }
    pthread_mutex_unlock(&log_lock);
}

/* ---------- part 2: the bounded queue ---------- */

typedef struct {
    char items[QUEUE_CAP][MSG_LEN];
    int head, tail, count;
    int producers_done;                          /* set when no more items will come */
    int total_produced, total_consumed;
    pthread_mutex_t lock;                        /* the "key" to the queue */
    pthread_cond_t not_full, not_empty;          /* the two "waiting rooms" */
} BoundedQueue;

static BoundedQueue bq = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
};

/* Producer side: add a message, wait if the queue is full */
static void queue_push(int producer_id, const char *msg)
{
    pthread_mutex_lock(&bq.lock);
    while (bq.count == QUEUE_CAP) {
        printf("  [FULL ] queue full (%d/%d) - producer %d waits\n",
               bq.count, QUEUE_CAP, producer_id);
        pthread_cond_wait(&bq.not_full, &bq.lock);   /* releases the lock while waiting */
    }
    snprintf(bq.items[bq.tail], MSG_LEN, "%s", msg);
    bq.tail = (bq.tail + 1) % QUEUE_CAP;
    bq.count++;
    bq.total_produced++;
    printf("  [PUSH ] producer %d added an entry   (queue %d/%d)\n",
           producer_id, bq.count, QUEUE_CAP);
    pthread_cond_signal(&bq.not_empty);          /* wake the consumer */
    pthread_mutex_unlock(&bq.lock);
}

/* Consumer side: returns 1 with a message, or 0 when everything is finished */
static int queue_pop(char *out)
{
    pthread_mutex_lock(&bq.lock);
    while (bq.count == 0 && !bq.producers_done) {
        printf("  [EMPTY] queue empty - consumer waits\n");
        pthread_cond_wait(&bq.not_empty, &bq.lock);
    }
    if (bq.count == 0) {                         /* empty and no more coming: stop */
        pthread_mutex_unlock(&bq.lock);
        return 0;
    }
    snprintf(out, MSG_LEN, "%s", bq.items[bq.head]);
    bq.head = (bq.head + 1) % QUEUE_CAP;
    bq.count--;
    bq.total_consumed++;
    printf("  [POP  ] consumer took an entry       (queue %d/%d)\n", bq.count, QUEUE_CAP);
    pthread_cond_signal(&bq.not_full);           /* wake a waiting producer */
    pthread_mutex_unlock(&bq.lock);
    return 1;
}

/* ---------- part 3: the threads ---------- */

typedef struct { int id; int items; } ProducerArg;

static void *producer_thread(void *arg)
{
    const ProducerArg *pa = arg;
    for (int k = 1; k <= pa->items; k++) {
        char msg[MSG_LEN];
        snprintf(msg, sizeof msg, "producer-%d: job request #%d submitted", pa->id, k);
        queue_push(pa->id, msg);
        usleep(5000 * (unsigned)pa->id);         /* producers work at different speeds */
    }
    return NULL;
}

static void *consumer_thread(void *arg)
{
    (void)arg;
    char msg[MSG_LEN];
    while (queue_pop(msg)) {
        log_write("LOGGER", "%s", msg);
        printf("  [LOG  ] written to %s: %s\n", LOG_PATH, msg);
        usleep(20000);                           /* slow consumer, so the queue fills up */
    }
    return NULL;
}

/* Reads a whole number between lo and hi. Returns 0 on success. */
static int parse_range(const char *s, int lo, int hi, int *out)
{
    char extra;
    int v;
    if (sscanf(s, "%d%c", &v, &extra) != 1 || v < lo || v > hi) return -1;
    *out = v;
    return 0;
}

int cmd_logger(int nargs, char *args[])
{
    const char *usage = "Usage: campuscompute logger [producers 1-10] [items_per_producer 1-50]";
    int producers = 3, items = 5;

    if (nargs > 2) {
        fprintf(stderr, "Error: too many arguments for 'logger'\n%s\n", usage);
        return 1;
    }
    if ((nargs >= 1 && parse_range(args[0], 1, MAX_PRODUCERS, &producers) != 0) ||
        (nargs == 2 && parse_range(args[1], 1, 50, &items) != 0)) {
        fprintf(stderr, "Error: invalid number\n%s\n", usage);
        return 1;
    }

    bq.head = bq.tail = bq.count = 0;            /* fresh queue for this run */
    bq.producers_done = 0;
    bq.total_produced = bq.total_consumed = 0;

    printf("Logger demo: %d producer threads x %d entries, 1 consumer thread, queue capacity %d\n",
           producers, items, QUEUE_CAP);
    log_write("LOGGER", "demo started: %d producers x %d entries, queue capacity %d",
              producers, items, QUEUE_CAP);
    fflush(stdout);

    pthread_t prod[MAX_PRODUCERS], cons;
    ProducerArg pargs[MAX_PRODUCERS];
    int created = 0, rc = 0;

    int err = pthread_create(&cons, NULL, consumer_thread, NULL);
    if (err != 0) {
        fprintf(stderr, "Error: pthread_create (consumer): %s\n", strerror(err));
        return 1;
    }
    for (int i = 0; i < producers; i++) {
        pargs[i].id = i + 1;
        pargs[i].items = items;
        err = pthread_create(&prod[i], NULL, producer_thread, &pargs[i]);
        if (err != 0) {
            fprintf(stderr, "Error: pthread_create (producer %d): %s\n", i + 1, strerror(err));
            rc = 1;
            break;
        }
        created++;
    }

    for (int i = 0; i < created; i++) pthread_join(prod[i], NULL);   /* wait for producers */
    printf("  [JOIN ] all %d producer threads finished\n", created);

    pthread_mutex_lock(&bq.lock);                /* tell the consumer: no more items */
    bq.producers_done = 1;
    pthread_cond_broadcast(&bq.not_empty);
    pthread_mutex_unlock(&bq.lock);

    pthread_join(cons, NULL);                    /* wait for the consumer */
    printf("  [JOIN ] consumer thread finished\n");

    printf("\nSummary: produced=%d consumed=%d (expected %d)\n",
           bq.total_produced, bq.total_consumed, producers * items);
    if (bq.total_produced != bq.total_consumed || bq.total_produced != producers * items) rc = 1;
    printf("All %d threads joined cleanly. Log file: %s\n", created + 1, LOG_PATH);
    log_write("LOGGER", "demo finished: produced=%d consumed=%d, all threads joined",
              bq.total_produced, bq.total_consumed);
    return rc;
}

/* ---------- part 4: show the end of the log file ---------- */

int cmd_showlog(int nargs, char *args[])
{
    int want = 25;
    if (nargs > 1 || (nargs == 1 && parse_range(args[0], 1, 200, &want) != 0)) {
        fprintf(stderr, "Error: invalid argument\nUsage: campuscompute showlog [lines 1-200]\n");
        return 1;
    }
    FILE *fp = fopen(LOG_PATH, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open %s: %s\n(run './campuscompute logger' first)\n",
                LOG_PATH, strerror(errno));
        return 1;
    }
    static char ring[200][256];
    int total = 0;
    while (fgets(ring[total % 200], sizeof ring[0], fp)) total++;
    fclose(fp);

    struct stat st;
    long size = (stat(LOG_PATH, &st) == 0) ? (long)st.st_size : -1;
    printf("Log file: %s (%d lines, %ld bytes). Showing last %d:\n\n", LOG_PATH, total, size,
           total < want ? total : want);
    int start = total > want ? total - want : 0;
    for (int i = start; i < total; i++) fputs(ring[i % 200], stdout);
    return 0;
}
