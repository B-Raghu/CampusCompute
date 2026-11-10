# CampusCompute - OS Level 3 Mini-Project (UE25CS242A)

Command-line prototype showing CPU scheduling, a threaded logger, POSIX IPC,
the Banker's algorithm, worker-failure detection and resource limits.
Written in C for Linux / Ubuntu.

## Prerequisites
- Ubuntu (or WSL Ubuntu) with gcc and make:
  sudo apt update && sudo apt install -y build-essential

## Build
    make clean
    make
This produces the executable ./campuscompute

## Quick Start
    make clean && make
    ./campuscompute help
    ./campuscompute all
    ./campuscompute showlog 15

## Run
./campuscompute help lists every command. Each feature runs on its own:

| Command | Feature |
|---|---|
| ./campuscompute jobs | Show the job set |
| ./campuscompute fcfs | First Come First Served |
| ./campuscompute rr [quantum] | Round Robin (default quantum 3) |
| ./campuscompute priority | Non-preemptive Priority |
| ./campuscompute compare [quantum] | All three policies on the same jobs |
| ./campuscompute logger [producers] [items] | Producer-consumer logger with pthreads |
| ./campuscompute showlog [lines] | Show the end of logs/campuscompute.log |
| ./campuscompute ipc | Shared memory + named semaphores |
| ./campuscompute ipc-sem | Named semaphore guarding a shared counter |
| ./campuscompute banker-safe | Banker's algorithm, safe state |
| ./campuscompute banker-unsafe | Banker's algorithm, unsafe state |
| ./campuscompute worker-normal | fork()/waitpid(), normal completion |
| ./campuscompute worker-fail | fork()/waitpid(), SIMULATED failures |
| ./campuscompute limits | setrlimit() demonstrations |
| ./campuscompute check | Verify processes, threads and IPC objects are cleaned up |
| ./campuscompute cleanup | Remove CampusCompute IPC objects left by a crash |
| ./campuscompute all | Run everything and print a PASS/FAIL summary |

Invalid commands or arguments print an error and a usage message.

## Testing
Each feature is independently testable with its own command (see the Run table).
All test inputs are built in and repeatable, so results are the same on every run.
./campuscompute all runs every test and prints a PASS/FAIL summary.

## Cleanup
- Every command removes the IPC objects it creates before it ends.
- ./campuscompute cleanup removes only these objects, in case a run was interrupted:
  shared memory /cc_shm, semaphores /cc_sem_data, /cc_sem_ack, /cc_sem_mutex.
- make clean removes the executable and object files.
- rm -rf logs removes the log folder created by this program.
- Unrelated files and other IPC objects are never touched.

## Design choices
- Tie-breaking rule (all policies): earlier arrival first, then lower job ID.
- Priority: a lower number means higher priority; scheduling is non-preemptive.
- Round Robin time quantum: 3 by default; jobs arriving during a slice join the
  queue before the preempted job returns to the tail.
- Built-in job set, written id(arrival, burst, priority):
  J1(0,5,2) J2(1,3,1) J3(2,8,3) J4(3,6,2) J5(4,2,1). It is the same on every run.
- Logger: 3 producer threads x 5 entries by default, 1 consumer thread, bounded
  queue of 4 entries, one mutex and two condition variables (not_full, not_empty).
  All threads are joined cleanly.
- IPC: POSIX shared memory (shm_open/mmap) with named semaphores (sem_open).
  Waits time out after 5 seconds so a crashed partner cannot hang the program.
- Banker's algorithm: 5 processes, 3 resource types, Need = Maximum - Allocation.
  Safe test: Available [3 3 2] gives safe sequence <P1, P3, P4, P0, P2>.
  Unsafe test: Available [0 1 1] leaves P0, P2 and P4 stuck.
- Simulated failures (not real OS faults): one worker exits with code 3 and another
  sends SIGTERM to itself. The parent detects both with waitpid().
- Resource limits (each test in its own child process): RLIMIT_NOFILE (open() fails
  with EMFILE), RLIMIT_AS (malloc fails with ENOMEM), RLIMIT_CPU (kernel sends SIGXCPU).

## Files
main.c (command-line menu) | scheduler.c (FCFS, Round Robin, Priority) |
logger.c (threads, log file) | ipc.c (shared memory, semaphores, check, cleanup) |
banker.c | worker.c (fork/waitpid) | limits.c (setrlimit) | all.c (integrated run) |
campuscompute.h (shared header) | Makefile
