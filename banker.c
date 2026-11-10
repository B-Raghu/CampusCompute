/* banker.c - resource-safety analysis with the Banker's safety algorithm
 *
 * Need = Maximum - Allocation.
 * Work starts as Available. Repeatedly look for an unfinished process whose
 * Need fits inside Work: it can finish, so it gives its Allocation back to Work.
 * If every process can finish this way, the order found is a SAFE SEQUENCE.
 */
#include "campuscompute.h"

#include <stdio.h>
#include <string.h>

#define BN 5    /* number of processes */
#define BM 3    /* number of resource types: A, B, C */

typedef struct {
    const char *title;
    int avail[BM];
    int alloc[BN][BM];
    int max[BN][BM];
} BankerCase;

/* Classic textbook state: SAFE. Expected safe sequence <P1, P3, P4, P0, P2>. */
static const BankerCase safe_case = {
    "SAFE test case",
    { 3, 3, 2 },
    { {0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2} },
    { {7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3} },
};

/* Same Allocation and Maximum, but only [0 1 1] is available:
 * P3 and P1 can finish, then P0, P2 and P4 are stuck -> UNSAFE. */
static const BankerCase unsafe_case = {
    "UNSAFE test case",
    { 0, 1, 1 },
    { {0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2} },
    { {7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3} },
};

static void vec_str(char *buf, size_t len, const int v[BM])
{
    snprintf(buf, len, "[%d %d %d]", v[0], v[1], v[2]);
}

/* Returns 1 if the state is safe (and fills seq[]), 0 if unsafe */
static int analyse(const BankerCase *c, int need[BN][BM], int seq[BN],
                   int finish[BN], int *count, int work[BM])
{
    memcpy(work, c->avail, sizeof(int) * BM);
    memset(finish, 0, sizeof(int) * BN);
    *count = 0;

    printf("\nRunning the safety algorithm (Work starts as Available):\n");
    int progress = 1;
    while (progress && *count < BN) {
        progress = 0;
        for (int i = 0; i < BN; i++) {
            if (finish[i]) continue;
            int fits = 1;                        /* does Need <= Work for every resource? */
            for (int j = 0; j < BM; j++)
                if (need[i][j] > work[j]) { fits = 0; break; }
            if (!fits) continue;

            char nb[24], wb[24], nw[24];
            vec_str(nb, sizeof nb, need[i]);
            vec_str(wb, sizeof wb, work);
            for (int j = 0; j < BM; j++) work[j] += c->alloc[i][j];   /* it finishes and gives back */
            vec_str(nw, sizeof nw, work);
            printf("  P%d can finish: Need %s <= Work %s -> releases Allocation, Work = %s\n",
                   i, nb, wb, nw);
            finish[i] = 1;
            seq[(*count)++] = i;
            progress = 1;
        }
    }
    return *count == BN;
}

/* Runs one case. Returns 0 if the result matches what the test expects. */
static int run_case(const BankerCase *c, int expect_safe)
{
    for (int i = 0; i < BN; i++)                 /* check the data is sensible */
        for (int j = 0; j < BM; j++)
            if (c->alloc[i][j] < 0 || c->alloc[i][j] > c->max[i][j] || c->avail[j] < 0) {
                fprintf(stderr, "Error: invalid Banker's data (Allocation must be 0..Maximum)\n");
                return 1;
            }

    int need[BN][BM], seq[BN], finish[BN], count, work[BM];
    char av[24];
    vec_str(av, sizeof av, c->avail);

    printf("=== Banker's algorithm: %s ===\n", c->title);
    printf("%d processes, %d resource types (A B C)\nAvailable = %s\n\n", BN, BM, av);
    printf("%-5s %-12s %-12s %-12s\n", "Proc", "Allocation", "Maximum", "Need");
    for (int i = 0; i < BN; i++) {
        char a[24], m[24], n[24];
        for (int j = 0; j < BM; j++) need[i][j] = c->max[i][j] - c->alloc[i][j];   /* Need = Max - Alloc */
        vec_str(a, sizeof a, c->alloc[i]);
        vec_str(m, sizeof m, c->max[i]);
        vec_str(n, sizeof n, need[i]);
        printf("P%-4d %-12s %-12s %-12s\n", i, a, m, n);
    }

    int safe = analyse(c, need, seq, finish, &count, work);

    printf("\n");
    if (safe) {
        printf("RESULT: SAFE state. Safe sequence: <");
        for (int i = 0; i < BN; i++) printf("%sP%d", i ? ", " : "", seq[i]);
        printf(">\n");
    } else {
        char wb[24];
        vec_str(wb, sizeof wb, work);
        printf("RESULT: UNSAFE state. Finished: ");
        for (int i = 0; i < count; i++) printf("%sP%d", i ? ", " : "", seq[i]);
        printf("; stuck (Need > Work %s): ", wb);
        int first = 1;
        for (int i = 0; i < BN; i++)
            if (!finish[i]) { printf("%sP%d", first ? "" : ", ", i); first = 0; }
        printf("\nNo safe sequence exists, so granting resources this way risks deadlock.\n");
    }
    log_write("BANKER", "%s: state is %s", c->title, safe ? "SAFE" : "UNSAFE");

    if (safe != expect_safe) {
        fprintf(stderr, "Unexpected result for this test case!\n");
        return 1;
    }
    printf("(This matches the expected result for this test case.)\n");
    return 0;
}

int cmd_banker_safe(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'banker-safe' takes no arguments\n");
        return 1;
    }
    return run_case(&safe_case, 1);
}

int cmd_banker_unsafe(int nargs, char *args[])
{
    (void)args;
    if (nargs > 0) {
        fprintf(stderr, "Error: 'banker-unsafe' takes no arguments\n");
        return 1;
    }
    return run_case(&unsafe_case, 0);
}
