#!/usr/bin/env bash
# tests/run_tests.sh - automated smoke tests for CampusCompute
#
# Usage (from anywhere):   bash tests/run_tests.sh
# Each test runs one command and checks its exit code and a key line of its output.
# All inputs are built into the program, so the results are the same on every run.

cd "$(dirname "$0")/.." || exit 1

echo "Building..."
make -s >/dev/null 2>&1 || { echo "Build failed - run 'make' to see the errors"; exit 1; }

BIN=./campuscompute
pass=0
fail=0

# check "test name" expected_exit_code "text that must appear" command [arguments...]
check() {
    local name="$1" want_rc="$2" want_text="$3"
    shift 3
    local out rc
    out=$("$@" 2>&1)
    rc=$?
    if [ "$rc" -eq "$want_rc" ] && printf '%s' "$out" | grep -qF -- "$want_text"; then
        printf '  PASS  %s\n' "$name"
        pass=$((pass + 1))
    else
        printf '  FAIL  %s (exit code %s, expected %s; looking for "%s")\n' "$name" "$rc" "$want_rc" "$want_text"
        fail=$((fail + 1))
    fi
}

echo
echo "Command-line interface (FR8)"
check "help shows usage"                 0 "Usage:"                          $BIN help
check "no command gives an error"        1 "no command given"                $BIN
check "unknown command gives an error"   1 "unknown command 'hello'"         $BIN hello
check "invalid quantum is rejected"      1 "invalid time quantum 'abc'"      $BIN rr abc
check "zero quantum is rejected"         1 "invalid time quantum '0'"        $BIN rr 0
check "invalid logger number rejected"   1 "invalid number"                  $BIN logger 99
check "extra argument is rejected"       1 "takes no arguments"              $BIN ipc extra

echo
echo "Job management and scheduling (FR1, FR2)"
check "jobs lists 5 jobs"                0 "Job set (5 jobs)"                $BIN jobs
check "FCFS average turnaround 13.00"    0 "= 13.00"                         $BIN fcfs
check "FCFS average waiting 8.20"        0 "= 8.20"                          $BIN fcfs
check "RR default quantum is 3"          0 "time quantum = 3"                $BIN rr
check "RR average waiting 9.60"          0 "= 9.60"                          $BIN rr
check "RR accepts quantum 5"             0 "time quantum = 5"                $BIN rr 5
check "Priority execution order"         0 "J1 -> J2 -> J5 -> J4 -> J3"      $BIN priority
check "Priority average waiting 5.80"    0 "= 5.80"                          $BIN priority
check "compare picks lowest waiting"     0 "Lowest average waiting time: Priority" $BIN compare

echo
echo "Threads and logger (FR3)"
check "logger produced = consumed"       0 "produced=6 consumed=6"           $BIN logger 2 3
check "logger joins all threads"         0 "joined cleanly"                  $BIN logger 2 3
check "showlog reads the log file"       0 "Log file:"                       $BIN showlog

echo
echo "Inter-process communication (FR4)"
check "shared-memory exchange"           0 "exchange completed successfully" $BIN ipc
check "semaphore-guarded counter"        0 "Final counter = 40000"           $BIN ipc-sem

echo
echo "Banker's algorithm (FR5)"
check "safe state and safe sequence"     0 "<P1, P3, P4, P0, P2>"            $BIN banker-safe
check "unsafe state detected"            0 "UNSAFE state"                    $BIN banker-unsafe

echo
echo "Worker processes and limits (FR6, FR7)"
check "normal workers complete"          0 "all 3 workers completed normally"   $BIN worker-normal
check "simulated failures detected"      0 "exactly 2 simulated failures"       $BIN worker-fail
check "resource limits take effect"      0 "all three limits had the expected effect" $BIN limits

echo
echo "Cleanup and integrated run (FR7)"
check "cleanup verification is clean"    0 "CLEAN"                           $BIN check
check "cleanup command runs"             0 "Removing CampusCompute IPC objects" $BIN cleanup
check "integrated run passes all 13"     0 "Total: 13 passed, 0 failed"      $BIN all

if ls /dev/shm 2>/dev/null | grep -q 'cc_'; then
    echo "  FAIL  no CampusCompute objects left in /dev/shm"
    fail=$((fail + 1))
else
    echo "  PASS  no CampusCompute objects left in /dev/shm"
    pass=$((pass + 1))
fi

echo
echo "Result: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
