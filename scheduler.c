/* scheduler.c - job management and CPU scheduling */
#include <stdio.h>

#define MAX_JOBS 20

typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_DONE } JobStatus;

typedef struct {
    int id;
    int arrival;     /* when the job arrives */
    int burst;       /* CPU time the job needs */
    int priority;    /* lower number = more important */
    JobStatus status;
    int completion;  /* filled in by the schedulers later */
    int turnaround;
    int waiting;
} Job;

static const char *status_name(JobStatus s)
{
    switch (s) {
    case JOB_NEW:     return "NEW";
    case JOB_READY:   return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE:    return "DONE";
    }
    return "?";
}

/* Fixed sample jobs so every run gives the same results */
static int load_default_jobs(Job jobs[])
{
    static const int data[5][4] = {
        /* id arrival burst priority */
        { 1, 0, 5, 2 },
        { 2, 1, 3, 1 },
        { 3, 2, 8, 3 },
        { 4, 3, 6, 2 },
        { 5, 4, 2, 1 },
    };
    for (int i = 0; i < 5; i++) {
        jobs[i].id = data[i][0];
        jobs[i].arrival = data[i][1];
        jobs[i].burst = data[i][2];
        jobs[i].priority = data[i][3];
        jobs[i].status = JOB_NEW;
        jobs[i].completion = 0;
        jobs[i].turnaround = 0;
        jobs[i].waiting = 0;
    }
    return 5;
}

int cmd_jobs(int nargs, char *args[])
{
    (void)nargs;
    (void)args;
    Job jobs[MAX_JOBS];
    int n = load_default_jobs(jobs);

    printf("Job set (%d jobs). Lower priority number = higher priority.\n", n);
    printf("%-5s %-8s %-6s %-9s %s\n", "Job", "Arrival", "Burst", "Priority", "Status");
    for (int i = 0; i < n; i++)
        printf("J%-4d %-8d %-6d %-9d %s\n", jobs[i].id, jobs[i].arrival,
               jobs[i].burst, jobs[i].priority, status_name(jobs[i].status));
    return 0;
}

/* ---------- helpers shared by all the schedulers ---------- */

#define MAX_SEGS 512

/* One block of the timeline: job_id ran from start to end (job_id 0 = CPU idle) */
typedef struct { int job_id; int start; int end; } Segment;
typedef struct { Segment seg[MAX_SEGS]; int count; } Timeline;

/* order[] = job positions sorted by arrival time; ties go to the lower job ID */
static void sort_by_arrival(const Job jobs[], int n, int order[])
{
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {
        int key = order[i], j = i - 1;
        while (j >= 0 && (jobs[order[j]].arrival > jobs[key].arrival ||
               (jobs[order[j]].arrival == jobs[key].arrival &&
                jobs[order[j]].id > jobs[key].id))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
}

/* Record that job_id used the CPU from start to end */
static void tl_add(Timeline *tl, int job_id, int start, int end)
{
    if (start >= end || tl->count >= MAX_SEGS) return;
    tl->seg[tl->count].job_id = job_id;
    tl->seg[tl->count].start = start;
    tl->seg[tl->count].end = end;
    tl->count++;
}

/* Fill in the results when a job finishes at time 'time' */
static void finish_job(Job *j, int time)
{
    j->completion = time;
    j->turnaround = j->completion - j->arrival;   /* total time in the system */
    j->waiting = j->turnaround - j->burst;        /* time spent waiting */
    j->status = JOB_DONE;
}

static void averages(const Job jobs[], int n, double *avg_tat, double *avg_wait)
{
    double tat = 0, wait = 0;
    for (int i = 0; i < n; i++) {
        tat += jobs[i].turnaround;
        wait += jobs[i].waiting;
    }
    *avg_tat = tat / n;
    *avg_wait = wait / n;
}

static void print_timeline(const Timeline *tl)
{
    printf("Execution timeline (Gantt chart):\n  ");
    for (int i = 0; i < tl->count; i++) {
        if (tl->seg[i].job_id == 0) printf("| IDLE ");
        else printf("| J%-3d ", tl->seg[i].job_id);
    }
    printf("|\n  ");
    for (int i = 0; i < tl->count; i++) printf("%-6d", tl->seg[i].start);
    if (tl->count > 0) printf("%d", tl->seg[tl->count - 1].end);

    printf("\nExecution order: ");
    int first = 1;
    for (int i = 0; i < tl->count; i++) {
        if (tl->seg[i].job_id == 0) continue;
        printf("%sJ%d", first ? "" : " -> ", tl->seg[i].job_id);
        first = 0;
    }
    printf("\n");
}

static void print_report(const char *title, const char *rule,
                         const Job jobs[], int n, const Timeline *tl)
{
    printf("\n=== %s ===\n%s\n\n", title, rule);
    print_timeline(tl);
    printf("\n%-5s %-8s %-6s %-9s %-11s %-11s %-8s %s\n", "Job", "Arrival",
           "Burst", "Priority", "Completion", "Turnaround", "Waiting", "Status");
    for (int i = 0; i < n; i++)
        printf("J%-4d %-8d %-6d %-9d %-11d %-11d %-8d %s\n", jobs[i].id,
               jobs[i].arrival, jobs[i].burst, jobs[i].priority,
               jobs[i].completion, jobs[i].turnaround, jobs[i].waiting,
               status_name(jobs[i].status));
    double tat, wait;
    averages(jobs, n, &tat, &wait);
    printf("\nAverage turnaround time = %.2f\n", tat);
    printf("Average waiting time    = %.2f\n", wait);
}

/* ---------- FCFS: First Come First Served ---------- */

static void schedule_fcfs(Job jobs[], int n, Timeline *tl)
{
    int order[MAX_JOBS];
    sort_by_arrival(jobs, n, order);

    int t = 0;                                   /* the clock */
    for (int k = 0; k < n; k++) {
        Job *j = &jobs[order[k]];
        if (t < j->arrival) {                    /* nobody is here yet: CPU idles */
            tl_add(tl, 0, t, j->arrival);
            t = j->arrival;
        }
        j->status = JOB_RUNNING;
        tl_add(tl, j->id, t, t + j->burst);      /* job runs until it is finished */
        t += j->burst;
        finish_job(j, t);
    }
}

int cmd_fcfs(int nargs, char *args[])
{
    (void)nargs;
    (void)args;
    Job jobs[MAX_JOBS];
    int n = load_default_jobs(jobs);
    Timeline tl = { .count = 0 };

    schedule_fcfs(jobs, n, &tl);
    print_report("FCFS (First Come First Served)",
                 "Rule: jobs run in order of arrival; ties go to the lower job ID.",
                 jobs, n, &tl);
    return 0;
}

/* ---------- Round Robin ---------- */

#define DEFAULT_QUANTUM 3

/* A simple circular queue that holds job positions */
typedef struct { int data[MAX_JOBS + 1]; int head, tail, count; } Queue;

static void q_push(Queue *q, int v)
{
    q->data[q->tail] = v;
    q->tail = (q->tail + 1) % (MAX_JOBS + 1);
    q->count++;
}

static int q_pop(Queue *q)
{
    int v = q->data[q->head];
    q->head = (q->head + 1) % (MAX_JOBS + 1);
    q->count--;
    return v;
}

/* Move every job that has arrived by time t into the ready queue */
static void admit_arrivals(Job jobs[], const int order[], int n, int *next,
                           int t, Queue *q)
{
    while (*next < n && jobs[order[*next]].arrival <= t) {
        jobs[order[*next]].status = JOB_READY;
        q_push(q, order[*next]);
        (*next)++;
    }
}

static void schedule_rr(Job jobs[], int n, int quantum, Timeline *tl)
{
    int order[MAX_JOBS], remaining[MAX_JOBS];
    sort_by_arrival(jobs, n, order);
    for (int i = 0; i < n; i++) remaining[i] = jobs[i].burst;

    Queue q = { .head = 0, .tail = 0, .count = 0 };
    int next = 0, t = 0, done = 0;

    admit_arrivals(jobs, order, n, &next, t, &q);
    while (done < n) {
        if (q.count == 0) {                       /* nobody ready: CPU idles */
            int arr = jobs[order[next]].arrival;
            tl_add(tl, 0, t, arr);
            t = arr;
            admit_arrivals(jobs, order, n, &next, t, &q);
            continue;
        }
        int i = q_pop(&q);                        /* take the job at the front */
        int run = remaining[i] < quantum ? remaining[i] : quantum;
        jobs[i].status = JOB_RUNNING;
        tl_add(tl, jobs[i].id, t, t + run);
        t += run;
        remaining[i] -= run;

        admit_arrivals(jobs, order, n, &next, t, &q);   /* new arrivals line up first */
        if (remaining[i] == 0) {
            finish_job(&jobs[i], t);
            done++;
        } else {
            jobs[i].status = JOB_READY;
            q_push(&q, i);                        /* not finished: back of the line */
        }
    }
}

int cmd_rr(int nargs, char *args[])
{
    int quantum = DEFAULT_QUANTUM;

    if (nargs > 1) {
        fprintf(stderr, "Error: too many arguments for 'rr'\n"
                        "Usage: campuscompute rr [quantum]\n");
        return 1;
    }
    if (nargs == 1) {
        char extra;
        if (sscanf(args[0], "%d%c", &quantum, &extra) != 1 || quantum < 1 || quantum > 1000) {
            fprintf(stderr, "Error: invalid time quantum '%s' (must be a whole number 1-1000)\n"
                            "Usage: campuscompute rr [quantum]\n", args[0]);
            return 1;
        }
    }

    Job jobs[MAX_JOBS];
    int n = load_default_jobs(jobs);
    Timeline tl = { .count = 0 };
    schedule_rr(jobs, n, quantum, &tl);

    char title[64], rule[160];
    snprintf(title, sizeof title, "Round Robin (time quantum = %d)", quantum);
    snprintf(rule, sizeof rule,
             "Rule: each job runs at most %d time units, then goes to the back of the line;\n"
             "ties go to the earlier arrival, then the lower job ID.", quantum);
    print_report(title, rule, jobs, n, &tl);
    return 0;
}

/* ---------- Priority scheduling (non-preemptive) ---------- */

/* Is job a a better pick than job b? Lower priority number wins;
 * ties go to the earlier arrival, then the lower job ID. */
static int better_priority(const Job *a, const Job *b)
{
    if (a->priority != b->priority) return a->priority < b->priority;
    if (a->arrival != b->arrival) return a->arrival < b->arrival;
    return a->id < b->id;
}

static void schedule_priority(Job jobs[], int n, Timeline *tl)
{
    int finished[MAX_JOBS] = { 0 };
    int t = 0, done = 0;

    while (done < n) {
        int best = -1;                            /* best job that has already arrived */
        for (int i = 0; i < n; i++) {
            if (!finished[i] && jobs[i].arrival <= t &&
                (best < 0 || better_priority(&jobs[i], &jobs[best])))
                best = i;
        }
        if (best < 0) {                           /* nobody here yet: CPU idles */
            int next = -1;
            for (int i = 0; i < n; i++)
                if (!finished[i] && (next < 0 || jobs[i].arrival < next))
                    next = jobs[i].arrival;
            tl_add(tl, 0, t, next);
            t = next;
            continue;
        }
        jobs[best].status = JOB_RUNNING;          /* runs to the end: never interrupted */
        tl_add(tl, jobs[best].id, t, t + jobs[best].burst);
        t += jobs[best].burst;
        finish_job(&jobs[best], t);
        finished[best] = 1;
        done++;
    }
}

int cmd_priority(int nargs, char *args[])
{
    (void)nargs;
    (void)args;
    Job jobs[MAX_JOBS];
    int n = load_default_jobs(jobs);
    Timeline tl = { .count = 0 };

    schedule_priority(jobs, n, &tl);
    print_report("Priority Scheduling (non-preemptive)",
                 "Rule: lowest priority number runs first; ties go to the earlier arrival,\n"
                 "then the lower job ID. A running job is never interrupted.",
                 jobs, n, &tl);
    return 0;
}

/* ---------- Comparison of all three policies ---------- */

int cmd_compare(int nargs, char *args[])
{
    int quantum = DEFAULT_QUANTUM;

    if (nargs > 1) {
        fprintf(stderr, "Error: too many arguments for 'compare'\n"
                        "Usage: campuscompute compare [quantum]\n");
        return 1;
    }
    if (nargs == 1) {
        char extra;
        if (sscanf(args[0], "%d%c", &quantum, &extra) != 1 || quantum < 1 || quantum > 1000) {
            fprintf(stderr, "Error: invalid time quantum '%s' (must be a whole number 1-1000)\n"
                            "Usage: campuscompute compare [quantum]\n", args[0]);
            return 1;
        }
    }

    /* Three separate copies of the SAME job set, one per policy */
    Job base[MAX_JOBS], a[MAX_JOBS], b[MAX_JOBS], c[MAX_JOBS];
    int n = load_default_jobs(base);
    for (int i = 0; i < n; i++) a[i] = b[i] = c[i] = base[i];

    Timeline t1 = { .count = 0 }, t2 = { .count = 0 }, t3 = { .count = 0 };
    schedule_fcfs(a, n, &t1);
    schedule_rr(b, n, quantum, &t2);
    schedule_priority(c, n, &t3);

    const char *names[3] = { "FCFS", "Round Robin", "Priority (non-preemptive)" };
    const Job *sets[3] = { a, b, c };
    const Timeline *tls[3] = { &t1, &t2, &t3 };

    printf("\n=== Scheduling comparison (same %d jobs, Round Robin quantum = %d) ===\n\n",
           n, quantum);
    printf("%-27s %-16s %-13s %-11s %s\n", "Policy", "Avg turnaround",
           "Avg waiting", "Total time", "CPU slices");

    int best = 0;
    double best_wait = 1e9;
    for (int r = 0; r < 3; r++) {
        double tat, wait;
        averages(sets[r], n, &tat, &wait);

        int total = 0, slices = 0;
        for (int i = 0; i < n; i++)
            if (sets[r][i].completion > total) total = sets[r][i].completion;
        for (int i = 0; i < tls[r]->count; i++)
            if (tls[r]->seg[i].job_id != 0) slices++;

        printf("%-27s %-16.2f %-13.2f %-11d %d\n", names[r], tat, wait, total, slices);
        if (wait < best_wait) { best_wait = wait; best = r; }
    }
    printf("\nLowest average waiting time: %s (%.2f)\n", names[best], best_wait);
    return 0;
}
