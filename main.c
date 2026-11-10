/* main.c - CampusCompute command-line entry point */
#include "campuscompute.h"
#include <stdio.h>
#include <string.h>

static const char *g_prog = "./campuscompute";

/* Table: command name -> function -> help text -> start a fresh log file? (1/0) */
typedef struct {
    const char *name;
    int (*fn)(int, char **);
    const char *help;
    int reset_log;
} Command;

static const Command commands[] = {
    { "jobs",     cmd_jobs,     "Show the job set", 0 },
    { "fcfs",     cmd_fcfs,     "First Come First Served scheduling", 0 },
    { "rr",       cmd_rr,       "Round Robin scheduling (optional quantum, default 3)", 0 },
    { "priority", cmd_priority, "Non-preemptive Priority scheduling", 0 },
    { "compare",  cmd_compare,  "Compare all three policies on the same jobs", 0 },
    { "logger",   cmd_logger,   "Producer-consumer logger with threads [producers] [items]", 1 },
    { "showlog",  cmd_showlog,  "Show the end of the log file [lines]", 0 },
    { "ipc",      cmd_ipc,      "Shared memory + named semaphores between two processes", 0 },
    { "ipc-sem",  cmd_ipc_sem,  "Named semaphore guarding a shared counter", 0 },
    { "check",    cmd_check,    "Verify processes, threads and IPC objects are cleaned up", 0 },
    { "cleanup",  cmd_cleanup,  "Remove CampusCompute IPC objects left by a crash", 0 },
    { "banker-safe",   cmd_banker_safe,   "Banker algorithm on a SAFE state", 0 },
    { "banker-unsafe", cmd_banker_unsafe, "Banker algorithm on an UNSAFE state", 0 },
    { "worker-normal", cmd_worker_normal, "fork() workers that finish normally", 0 },
    { "worker-fail",   cmd_worker_fail,   "fork() workers with SIMULATED failures", 0 },
    { "limits",        cmd_limits,        "setrlimit() demonstrations", 0 },
    { "all",           cmd_all,           "Run every demonstration and print a summary", 0 },
    /* NEW COMMANDS GO ABOVE THIS LINE */
};

static void print_usage(FILE *out)
{
    fprintf(out, "CampusCompute - OS Mini Project\n");
    fprintf(out, "Usage: %s <command> [arguments]\n\n", g_prog);
    fprintf(out, "Commands:\n");
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++)
        fprintf(out, "  %-14s %s\n", commands[i].name, commands[i].help);
    fprintf(out, "  %-14s %s\n", "help", "Show this help message");
}

int main(int argc, char *argv[])
{
    g_prog = argv[0];

    if (argc < 2) {
        fprintf(stderr, "Error: no command given\n\n");
        print_usage(stderr);
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "help") == 0) {
        print_usage(stdout);
        return 0;
    }

    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            if (commands[i].reset_log) log_reset();
            return commands[i].fn(argc - 2, argv + 2);
        }
    }

    fprintf(stderr, "Error: unknown command '%s'\n\n", cmd);
    print_usage(stderr);
    return 1;
}
