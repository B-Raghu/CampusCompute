/* campuscompute.h - shared declarations for all CampusCompute files */
#ifndef CAMPUSCOMPUTE_H
#define CAMPUSCOMPUTE_H

#include <stddef.h>

#define LOG_DIR   "logs"
#define LOG_PATH  "logs/campuscompute.log"

/* logger.c: shared log-file writer, usable from any file */
void log_reset(void);
void log_write(const char *module, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Each command receives the arguments typed AFTER the command name.
 * It returns 0 on success and non-zero on error. */
int cmd_jobs(int nargs, char *args[]);
int cmd_fcfs(int nargs, char *args[]);
int cmd_rr(int nargs, char *args[]);
int cmd_priority(int nargs, char *args[]);
int cmd_compare(int nargs, char *args[]);
int cmd_logger(int nargs, char *args[]);
int cmd_showlog(int nargs, char *args[]);
int cmd_ipc(int nargs, char *args[]);
int cmd_ipc_sem(int nargs, char *args[]);
int cmd_check(int nargs, char *args[]);
int cmd_cleanup(int nargs, char *args[]);
int cmd_banker_safe(int nargs, char *args[]);
int cmd_banker_unsafe(int nargs, char *args[]);
int cmd_worker_normal(int nargs, char *args[]);
int cmd_worker_fail(int nargs, char *args[]);
int cmd_limits(int nargs, char *args[]);
/* worker.c: turns a wait status into readable text (used by limits.c too) */
void describe_status(int status, char *buf, size_t len);
int cmd_all(int nargs, char *args[]);
/* NEW DECLARATIONS GO ABOVE THIS LINE */

#endif
