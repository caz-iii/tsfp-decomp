// Address range: 0x80135200 - 0x80135440 | batch_364 | 2026-09-05
//
// Small dense iterative constrained linear solver. Used to resolve a set of
// bounded scalar unknowns against an NxN system matrix (N is small, <= 8:
// the matrix rows are stored with a fixed 0x20-byte / 8-float stride). The
// solver produces x[i] clamped between per-row lo[]/hi[] bounds and relaxes
// the residual for a bounded number of passes (Gauss-Seidel style).
//
// Functions:
//   solver_matrix_vector_multiply 0x80135200  y = M * x  (row stride 0x20)
//   solver_iterative_constrained  0x80135270  bounded relaxation solver

#include "common.h"

// --- Raw scratch-frame register save/restore helpers (0x8038xxxx). ----------
// The "enter" helper returns a packed (matrixPtr<<32)|solutionPtr pair in the
// r3:r4 ABI; we split it back apart at the call site.
extern unsigned long long FUN_803826d0(void);   // enter: reserve fp scratch frame
extern void               FUN_8038271c(void);    // leave: release fp scratch frame

// --- Per-dimension scale constant table (indexed by N). --------------------
extern float DAT_8040db20;

// ---------------------------------------------------------------------------
// solver_matrix_vector_multiply
//
// out[i] = sum_j mtx[i][j] * vec[j]   for i,j in [0, n)
// The matrix is row-major with an 8-float (0x20-byte) stride between rows;
// only the first n columns of each row participate. out and vec are packed
// float arrays (4-byte stride).
// ---------------------------------------------------------------------------
void solver_matrix_vector_multiply(int out, int mtx, float *vec, int n)
{
    float v0;
    float *pCoeff;
    float *pRow;
    int remaining;
    int outOff;
    int rowBase;
    int colOff;
    int i;

    v0 = *vec;
    outOff = 0;
    rowBase = 0;
    for (i = 0; i < n; i = i + 1) {
        remaining = n + -1;
        colOff = 4;
        *(float *)(out + outOff) = v0 * *(float *)(mtx + rowBase);
        if (1 < n) {
            do {
                pCoeff = (float *)((int)vec + colOff);
                pRow = (float *)(mtx + rowBase + colOff);
                colOff = colOff + 4;
                *(float *)(out + outOff) = *(float *)(out + outOff) + *pCoeff * *pRow;
                remaining = remaining + -1;
            } while (remaining != 0);
        }
        outOff = outOff + 4;
        rowBase = rowBase + 0x20;   // next matrix row (8-float stride)
    }
    return;
}

// ---------------------------------------------------------------------------
// solver_iterative_constrained
//
// Bounded linear solve of an n-dimensional system.
//   rhs        (param_3) : right-hand-side vector b[]
//   hi_bounds  (param_4) : per-unknown upper clamp
//   lo_bounds  (param_5) : per-unknown lower clamp
//   n          (param_6) : dimension (n == 1 is the trivial divide case)
//   max_iters  (param_7) : relaxation pass cap
//
// The matrix pair (system matrix + solution buffer) is obtained from the fp
// scratch-frame helper. Each pass recomputes residual = M * x, and where the
// weighted correction exceeds an epsilon, applies it and re-clamps between the
// lo/hi bounds. Converges early when no row exceeds epsilon.
//
// param_1/param_2 are the caller-supplied working matrix and diagonal buffers
// (passed through in the raw ABI); typed as u32 to preserve their pointer use.
// ---------------------------------------------------------------------------
void solver_iterative_constrained(u32 param_1, u32 param_2, float *rhs, int hi_bounds,
                                  int lo_bounds, int n, int max_iters)
{
    float scale;
    float bVal;
    float loB;
    float hiB;
    float clamped;
    bool converged;
    float *diag;
    int accumOff;
    float *soln;
    int off;
    int row;
    double residual;
    double eps;
    float local_88[8];    // residual scratch (M*x)
    float local_68[24];   // per-row weight w[i] = scale / diag[i]

    // Reserve fp scratch frame; the returned packed pair is (diag<<32)|soln.
    unsigned long long frame = FUN_803826d0();
    diag = (float *)((unsigned long long)frame >> 0x20);
    soln = (float *)(unsigned int)frame;

    if (n == 1) {
        *soln = -*rhs / *diag;
    }
    else {
        scale = (&DAT_8040db20)[n];
        converged = true;
        accumOff = 0;
        off = 0;
        row = n;
        if (0 < n) {
            do {
                bVal = *(float *)((int)rhs + off);
                loB = *(float *)(lo_bounds + off);
                hiB = *(float *)(hi_bounds + off);
                *(float *)((int)local_68 + off) = scale / *(float *)((int)diag + accumOff + off);
                bVal = bVal * *(float *)((int)local_68 + off);
                clamped = bVal;
                if (loB < bVal) {
                    clamped = loB;
                }
                if ((hiB <= clamped) && (hiB = bVal, loB < bVal)) {
                    hiB = loB;
                }
                *(float *)((int)soln + off) = hiB;
                accumOff = accumOff + 0x20;
                off = off + 4;
                row = row + -1;
            } while (row != 0);
        }

        int iter = 0;
        eps = 0.0;   // convergence epsilon carried in fp scratch; treat as 0 baseline
        while ((iter < max_iters) && (!converged)) {
            converged = true;
            solver_matrix_vector_multiply((int)local_88, (int)diag, soln, n);
            off = 0;
            row = n;
            if (0 < n) {
                do {
                    residual = (double)(*(float *)((int)local_68 + off) *
                                        (*(float *)((int)rhs + off) +
                                         *(float *)((int)local_88 + off)));
                    if (eps < ABS(residual)) {
                        converged = false;
                        loB = *(float *)(lo_bounds + off);
                        bVal = (float)(residual + (double)*(float *)((int)soln + off));
                        hiB = *(float *)(hi_bounds + off);
                        clamped = bVal;
                        if (loB < bVal) {
                            clamped = loB;
                        }
                        if ((hiB <= clamped) && (hiB = bVal, loB < bVal)) {
                            hiB = loB;
                        }
                        *(float *)((int)soln + off) = hiB;
                    }
                    off = off + 4;
                    row = row + -1;
                } while (row != 0);
            }
            iter = iter + 1;
        }
    }
    FUN_8038271c();
    return;
}
