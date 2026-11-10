/* all.c - integrated demonstration: runs every feature and prints a PASS/FAIL summary */
#include "campuscompute.h"

#include <stdio.h>

typedef struct { const char *label; int (*fn)(int, char **); } Step;

int cmd_all(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'all' takes no arguments\n");
        return 1;
    }

    static const Step steps[] = {
        { "Scheduling: FCFS",            cmd_fcfs },
        { "Scheduling: Round Robin",     cmd_rr },
        { "Scheduling: Priority",        cmd_priority },
        { "Scheduling: comparison",      cmd_compare },
        { "Threads: producer-consumer",  cmd_logger },
        { "IPC: shared memory",          cmd_ipc },
        { "IPC: semaphore coordination", cmd_ipc_sem },
        { "Banker's: safe state",        cmd_banker_safe },
        { "Banker's: unsafe state",      cmd_banker_unsafe },
        { "Workers: normal",             cmd_worker_normal },
        { "Workers: simulated failures", cmd_worker_fail },
        { "Resource limits",             cmd_limits },
        { "Cleanup verification",        cmd_check },
    };
    const int total = (int)(sizeof steps / sizeof steps[0]);
    int results[sizeof steps / sizeof steps[0]];

    log_reset();                                  /* start a fresh log for this run */
    log_write("ALL", "integrated run started (%d steps)", total);

    for (int i = 0; i < total; i++) {
        printf("\n################ [%d/%d] %s ################\n", i + 1, total, steps[i].label);
        fflush(stdout);
        results[i] = steps[i].fn(0, NULL);        /* 0 means the step behaved as expected */
    }

    int passed = 0;
    printf("\n################ INTEGRATED SUMMARY ################\n");
    for (int i = 0; i < total; i++) {
        printf("  %2d. %-30s %s\n", i + 1, steps[i].label, results[i] == 0 ? "PASS" : "FAIL");
        if (results[i] == 0) passed++;
    }
    printf("\nTotal: %d passed, %d failed\n", passed, total - passed);
    log_write("ALL", "integrated run finished: %d passed, %d failed", passed, total - passed);

    FILE *fp = fopen(LOG_PATH, "r");              /* count the lines of the log file */
    if (fp) {
        int lines = 0, c;
        while ((c = fgetc(fp)) != EOF)
            if (c == '\n') lines++;
        fclose(fp);
        printf("Log file: %s (%d lines). View it with: ./campuscompute showlog\n", LOG_PATH, lines);
    }
    return passed == total ? 0 : 1;
}
