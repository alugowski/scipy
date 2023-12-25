#ifndef __CSR_H__
#define __CSR_H__

#include <set>
#include <vector>
#include <algorithm>
#include <functional>
#include <numeric>

#include "util.h"
#include "util_par.h"
#include "dense.h"

/*
 * Extract k-th diagonal of CSR matrix A
 *
 * Input Arguments:
 *   I  k             - diagonal to extract
 *   I  n_row         - number of rows in A
 *   I  n_col         - number of columns in A
 *   I  Ap[n_row+1]   - row pointer
 *   I  Aj[nnz(A)]    - column indices
 *   T  Ax[n_col]     - nonzeros
 *
 * Output Arguments:
 *   T  Yx[min(n_row,n_col)] - diagonal entries
 *
 * Note:
 *   Output array Yx must be preallocated
 *
 *   Duplicate entries will be summed.
 *
 *   Complexity: Linear.  Specifically O(nnz(A) + min(n_row,n_col))
 *
 */
template <class I, class T>
void csr_diagonal(const I k,
                  const I n_row,
                  const I n_col,
                  const I Ap[],
                  const I Aj[],
                  const T Ax[],
                        T Yx[])
{
    const I first_row = (k >= 0) ? 0 : -k;
    const I first_col = (k >= 0) ? k : 0;
    const I N = std::min(n_row - first_row, n_col - first_col);

    for (I i = 0; i < N; ++i) {
        const I row = first_row + i;
        const I col = first_col + i;
        const I row_begin = Ap[row];
        const I row_end = Ap[row + 1];

        T diag = 0;
        for (I j = row_begin; j < row_end; ++j) {
            if (Aj[j] == col) {
                diag += Ax[j];
            }
        }

        Yx[i] = diag;
    }

}


/*
 * Expand a compressed row pointer into a row array
 *
 * Input Arguments:
 *   I  n_row         - number of rows in A
 *   I  Ap[n_row+1]   - row pointer
 *
 * Output Arguments:
 *   Bi  - row indices
 *
 * Note:
 *   Output array Bi must be preallocated
 *
 * Note:
 *   Complexity: Linear
 *
 */
template <class I>
void expandptr(const I n_row,
               const I Ap[],
                     I Bi[])
{
    for(I i = 0; i < n_row; i++){
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            Bi[jj] = i;
        }
    }
}


/*
 * Scale the rows of a CSR matrix *in place*
 *
 *   A[i,:] *= X[i]
 *
 */
template <class I, class T>
void csr_scale_rows(const I n_row,
                    const I n_col,
                    const I Ap[],
                    const I Aj[],
                          T Ax[],
                    const T Xx[])
{
    for(I i = 0; i < n_row; i++){
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            Ax[jj] *= Xx[i];
        }
    }
}


/*
 * Scale the columns of a CSR matrix *in place*
 *
 *   A[:,i] *= X[i]
 *
 */
template <class I, class T>
void csr_scale_columns(const I n_row,
                       const I n_col,
                       const I Ap[],
                       const I Aj[],
                             T Ax[],
                       const T Xx[])
{
    const I nnz = Ap[n_row];
    for(I i = 0; i < nnz; i++){
        Ax[i] *= Xx[Aj[i]];
    }
}


/*
 * Compute the number of occupied RxC blocks in a matrix
 *
 * Input Arguments:
 *   I  n_row         - number of rows in A
 *   I  R             - row blocksize
 *   I  C             - column blocksize
 *   I  Ap[n_row+1]   - row pointer
 *   I  Aj[nnz(A)]    - column indices
 *
 * Output Arguments:
 *   I  num_blocks    - number of blocks
 *
 * Note:
 *   Complexity: Linear
 *
 */
template <class I>
I csr_count_blocks(const I n_row,
                   const I n_col,
                   const I R,
                   const I C,
                   const I Ap[],
                   const I Aj[])
{
    std::vector<I> mask(n_col/C + 1,-1);
    I n_blks = 0;
    for(I i = 0; i < n_row; i++){
        I bi = i/R;
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            I bj = Aj[jj]/C;
            if(mask[bj] != bi){
                mask[bj] = bi;
                n_blks++;
            }
        }
    }
    return n_blks;
}


/*
 * Convert a CSR matrix to BSR format
 *
 * Input Arguments:
 *   I  n_row           - number of rows in A
 *   I  n_col           - number of columns in A
 *   I  R               - row blocksize
 *   I  C               - column blocksize
 *   I  Ap[n_row+1]     - row pointer
 *   I  Aj[nnz(A)]      - column indices
 *   T  Ax[nnz(A)]      - nonzero values
 *
 * Output Arguments:
 *   I  Bp[n_row/R + 1] - block row pointer
 *   I  Bj[nnz(B)]      - column indices
 *   T  Bx[nnz(B)]      - nonzero blocks
 *
 * Note:
 *   Complexity: Linear
 *   Output arrays must be preallocated (with Bx initialized to zero)
 *
 *
 */
template <class I, class T>
void csr_tobsr(const I n_row,
               const I n_col,
               const I R,
               const I C,
               const I Ap[],
               const I Aj[],
               const T Ax[],
                     I Bp[],
                     I Bj[],
                     T Bx[])
{
    std::vector<T*> blocks(n_col/C + 1, (T*)0 );

    assert( n_row % R == 0 );
    assert( n_col % C == 0 );

    I n_brow = n_row / R;
    //I n_bcol = n_col / C;

    I RC = R*C;
    I n_blks = 0;

    Bp[0] = 0;

    for(I bi = 0; bi < n_brow; bi++){
        for(I r = 0; r < R; r++){
            I i = R*bi + r;  //row index
            for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
                I j = Aj[jj]; //column index

                I bj = j / C;
                I c  = j % C;

                if( blocks[bj] == 0 ){
                    blocks[bj] = Bx + RC*n_blks;
                    Bj[n_blks] = bj;
                    n_blks++;
                }

                *(blocks[bj] + C*r + c) += Ax[jj];
            }
        }

        for(I jj = Ap[R*bi]; jj < Ap[R*(bi+1)]; jj++){
            blocks[Aj[jj] / C] = 0;
        }

        Bp[bi+1] = n_blks;
    }
}


/*
 * Compute B += A for CSR matrix A, C-contiguous dense matrix B
 *
 * Input Arguments:
 *   I  n_row           - number of rows in A
 *   I  n_col           - number of columns in A
 *   I  Ap[n_row+1]     - row pointer
 *   I  Aj[nnz(A)]      - column indices
 *   T  Ax[nnz(A)]      - nonzero values
 *   T  Bx[n_row*n_col] - dense matrix in row-major order
 *
 */
template <class I, class T>
void csr_todense(const I n_row,
                 const I n_col,
                 const I Ap[],
                 const I Aj[],
                 const T Ax[],
                       T Bx[])
{
    T * Bx_row = Bx;
    for(I i = 0; i < n_row; i++){
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            Bx_row[Aj[jj]] += Ax[jj];
        }
        Bx_row += (npy_intp)n_col;
    }
}


/*
 * Determine whether the CSR column indices are in sorted order.
 *
 * Input Arguments:
 *   I  n_row           - number of rows in A
 *   I  Ap[n_row+1]     - row pointer
 *   I  Aj[nnz(A)]      - column indices
 *
 */
template <class I>
bool csr_has_sorted_indices(const I n_row,
                            const I Ap[],
                            const I Aj[])
{
  for(I i = 0; i < n_row; i++){
      for(I jj = Ap[i]; jj < Ap[i+1] - 1; jj++){
          if(Aj[jj] > Aj[jj+1]){
              return false;
          }
      }
  }
  return true;
}



/*
 * Determine whether the matrix structure is canonical CSR.
 * Canonical CSR implies that column indices within each row
 * are (1) sorted and (2) unique.  Matrices that meet these
 * conditions facilitate faster matrix computations.
 *
 * Input Arguments:
 *   I  n_row           - number of rows in A
 *   I  Ap[n_row+1]     - row pointer
 *   I  Aj[nnz(A)]      - column indices
 *
 */
template <class I>
bool csr_has_canonical_format(const I n_row,
                              const I Ap[],
                              const I Aj[])
{
    for(I i = 0; i < n_row; i++){
        if (Ap[i] > Ap[i+1])
            return false;
        for(I jj = Ap[i] + 1; jj < Ap[i+1]; jj++){
            if( !(Aj[jj-1] < Aj[jj]) ){
                return false;
            }
        }
    }
    return true;
}


template< class T1, class T2 >
bool kv_pair_less(const std::pair<T1,T2>& x, const std::pair<T1,T2>& y){
    return x.first < y.first;
}

/*
 * Sort CSR column indices inplace
 *
 * Input Arguments:
 *   I  n_row           - number of rows in A
 *   I  Ap[n_row+1]     - row pointer
 *   I  Aj[nnz(A)]      - column indices
 *   T  Ax[nnz(A)]      - nonzeros
 *
 */
template<class I, class T>
void csr_sort_indices(const I n_row,
                      const I Ap[],
                            I Aj[],
                            T Ax[])
{
    std::for_each(workers.par_if_flops(Ap[n_row]),
                  iota_iter<I>(0), iota_iter<I>(n_row), [&](I i){
        I row_start = Ap[i];
        I row_end   = Ap[i+1];

        std::sort(kv_pair_iters(Aj + row_start, Ax + row_start),
                  kv_pair_iters(Aj + row_end, Ax + row_end),
                  kv_pair_less<I,T>);
    });
}




/*
 * Compute B = A for CSR matrix A, CSC matrix B
 *
 * Also, with the appropriate arguments can also be used to:
 *   - compute B = A^t for CSR matrix A, CSR matrix B
 *   - compute B = A^t for CSC matrix A, CSC matrix B
 *   - convert CSC->CSR
 *
 * Input Arguments:
 *   I  n_row         - number of rows in A
 *   I  n_col         - number of columns in A
 *   I  Ap[n_row+1]   - row pointer
 *   I  Aj[nnz(A)]    - column indices
 *   T  Ax[nnz(A)]    - nonzeros
 *
 * Output Arguments:
 *   I  Bp[n_col+1] - column pointer
 *   I  Bi[nnz(A)]  - row indices
 *   T  Bx[nnz(A)]  - nonzeros
 *
 * Note:
 *   Output arrays Bp, Bi, Bx must be preallocated
 *
 * Note:
 *   Input:  column indices *are not* assumed to be in sorted order
 *   Output: row indices *will be* in sorted order
 *
 *   Complexity: Linear.  Specifically O(nnz(A) + max(n_row,n_col))
 *
 */
template <class I, class T>
void csr_tocsc(const I n_row,
               const I n_col,
               const I Ap[],
               const I Aj[],
               const T Ax[],
                     I Bp[],
                     I Bi[],
                     T Bx[])
{
    const I nnz = Ap[n_row];

    //compute number of non-zero entries per column of A
    std::fill(Bp, Bp + n_col + 1, 0);

    for (I n = 0; n < nnz; n++){
        Bp[Aj[n]]++;
    }

    //cumsum the nnz per column to get Bp[]
    exclusive_scan(Bp, Bp + n_col + 1, Bp, 0);

    for(I row = 0; row < n_row; row++){
        for(I jj = Ap[row]; jj < Ap[row+1]; jj++){
            I col  = Aj[jj];
            I dest = Bp[col];

            Bi[dest] = row;
            Bx[dest] = Ax[jj];

            Bp[col]++;
        }
    }

    for(I col = 0, last = 0; col <= n_col; col++){
        I temp  = Bp[col];
        Bp[col] = last;
        last    = temp;
    }
}



/*
 * Compute B = A for CSR matrix A, ELL matrix B
 *
 * Input Arguments:
 *   I  n_row         - number of rows in A
 *   I  n_col         - number of columns in A
 *   I  Ap[n_row+1]   - row pointer
 *   I  Aj[nnz(A)]    - column indices
 *   T  Ax[nnz(A)]    - nonzeros
 *   I  row_length    - maximum nnz in a row of A
 *
 * Output Arguments:
 *   I  Bj[n_row * row_length]  - column indices
 *   T  Bx[n_row * row_length]  - nonzeros
 *
 * Note:
 *   Output arrays Bj, Bx must be preallocated
 *   Duplicate entries in A are not merged.
 *   Explicit zeros in A are carried over to B.
 *   Rows with fewer than row_length columns are padded with zeros.
 *
 */
template <class I, class T>
void csr_toell(const I n_row,
               const I n_col,
               const I Ap[],
               const I Aj[],
               const T Ax[],
               const I row_length,
                     I Bj[],
                     T Bx[])
{
    const npy_intp ell_nnz = (npy_intp)row_length * n_row;
    std::fill(Bj, Bj + ell_nnz, 0);
    std::fill(Bx, Bx + ell_nnz, 0);

    for(I i = 0; i < n_row; i++){
        I * Bj_row = Bj + (npy_intp)row_length * i;
        T * Bx_row = Bx + (npy_intp)row_length * i;
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            *Bj_row = Aj[jj];
            *Bx_row = Ax[jj];
            Bj_row++;
            Bx_row++;
        }
    }
}


/*
 * Compute C = A*B for CSR matrices A,B
 *
 *
 * Input Arguments:
 *   I  n_row       - number of rows in A
 *   I  n_col       - number of columns in B (hence C is n_row by n_col)
 *   I  Ap[n_row+1] - row pointer
 *   I  Aj[nnz(A)]  - column indices
 *   T  Ax[nnz(A)]  - nonzeros
 *   I  Bp[?]       - row pointer
 *   I  Bj[nnz(B)]  - column indices
 *   T  Bx[nnz(B)]  - nonzeros
 * Output Arguments:
 *   I  Cp[n_row+1] - row pointer
 *   I  Cj[nnz(C)]  - column indices
 *   T  Cx[nnz(C)]  - nonzeros
 *
 * Note:
 *   Output arrays Cp, Cj, and Cx must be preallocated.
 *   In order to find the appropriate type for T, csr_matmat_maxnnz can be used
 *   to find nnz(C).
 *
 * Note:
 *   Input:  A and B column indices *are not* assumed to be in sorted order
 *   Output: C column indices *are not* assumed to be in sorted order
 *           Cx will not contain any zero entries
 *
 *   Complexity: O(n_row*K^2 + max(n_row,n_col))
 *                 where K is the maximum nnz in a row of A
 *                 and column of B.
 *
 *
 *  This is an implementation of the SMMP algorithm:
 *
 *    "Sparse Matrix Multiplication Package (SMMP)"
 *      Randolph E. Bank and Craig C. Douglas
 *
 *    http://citeseer.ist.psu.edu/445062.html
 *    http://www.mgnet.org/~douglas/ccd-codes.html
 *
 */

/*
 * Compute the total number of non-zeroes (nnz) in the result of C = A * B,
 * as well as the per-row nnz. The per-row nnz is offset by 1 so that
 * np.cumsum (or std::inclusive_scan) will compute Cp.
 */
template <class I, class T>
npy_intp csr_matmat_maxnnz(const I n_row,
                           const I n_col,
                           const I Ap[],
                           const I Aj[],
                           const T Ax[],
                           const I Bp[],
                           const I Bj[],
                           const T Bx[],
                                 I C_row_nnz[])
{
    // method uses O(n * num_threads) temp storage

    C_row_nnz[0] = 0;

    // Group the rows into chunks, with each chunk having its own workspace.
    // Chunks are independent and can be processed in parallel.
    for_each_chunk(workers.par_if_flops(Ap[n_row] + Bp[n_col]),
                   iota_iter<I>(0), iota_iter<I>(n_row), [&n_col]{
        // create workspace for one chunk of rows
        return make_pair(
            std::vector<I>(n_col, -1),  // mask
            std::vector<T>(n_col, 0));  // sums
    }, [&](I i, auto& workspace){
        auto& [mask, sums] = workspace;

        I row_nnz = 0;

        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            I j = Aj[jj];
            T v = Ax[jj];
            for(I kk = Bp[j]; kk < Bp[j+1]; kk++){
                I k = Bj[kk];
                if(mask[k] != i){
                    T sum = v*Bx[kk];
                    if (sum != 0){
                        sums[k] = sum;
                        mask[k] = i;
                        row_nnz++;
                    }
                }else{
                    T sum = sums[k];
                    sum += v*Bx[kk];
                    if(sum != 0){
                        sums[k] = sum;
                    }else{
                        mask[k] = -1;
                        row_nnz--;
                    }
                }
            }
        }

        C_row_nnz[i+1] = row_nnz;
    });

    // compute C nnz while testing for overflow
    npy_intp nnz = 0;
    for(I i = 0; i < n_row; i++){
        npy_intp row_nnz = C_row_nnz[i+1];

        if (row_nnz > NPY_MAX_INTP - nnz) {
            /*
             * Index overflowed. Note that row_nnz <= n_col and cannot overflow
             */
            throw std::overflow_error("nnz of the result is too large");
        }

        nnz += row_nnz;
    }

    return nnz;
}

/*
 * Compute CSR entries for matrix C = A*B.
 *
 * NOTE: Cp must have already been precomputed by csr_matmat_maxnnz followed by
 * np.cumsum or std::inclusive_scan.
 */
template <class I, class T>
void csr_matmat(const I n_row,
                const I n_col,
                const I Ap[],
                const I Aj[],
                const T Ax[],
                const I Bp[],
                const I Bj[],
                const T Bx[],
                const I Cp[],
                      I Cj[],
                      T Cx[])
{
    // method uses O(n * num_threads) temp storage

    // Group the rows into chunks, with each chunk having its own workspace.
    // Chunks are independent and can be processed in parallel.
    for_each_chunk(workers.par_if_flops(Ap[n_row] + Bp[n_col]),
                   iota_iter<I>(0), iota_iter<I>(n_row), [&n_col]{
        // create workspace for one chunk of rows
        return make_pair(
            std::vector<I>(n_col, -1),  // next
            std::vector<T>(n_col, 0));  // sums
    }, [&](I i, auto& workspace){
        auto& [next, sums] = workspace;

        I nnz = Cp[i];
        I head   = -2;
        I length =  0;

        I jj_start = Ap[i];
        I jj_end   = Ap[i+1];
        for(I jj = jj_start; jj < jj_end; jj++){
            I j = Aj[jj];
            T v = Ax[jj];

            I kk_start = Bp[j];
            I kk_end   = Bp[j+1];
            for(I kk = kk_start; kk < kk_end; kk++){
                I k = Bj[kk];

                sums[k] += v*Bx[kk];

                if(next[k] == -1){
                    next[k] = head;
                    head  = k;
                    length++;
                }
            }
        }

        // Populate Cj indices from linked list `next`.
        I start = Cp[i];
        I end = Cp[i+1];
        // If (length == expected row size) then all sums are nonzero
        // and do not need to be individually tested.
        bool row_is_all_nz = (length == end - start);
        for(I jj = 0; jj < length; jj++){
            if(row_is_all_nz || sums[head] != 0){
                Cj[nnz] = head;
                nnz++;
            }
            I temp = head;
            head = next[head];

            next[temp] = -1; //clear array
        }

        // Sort nonzeros so C is in canonical form
        std::sort(&Cj[start], &Cj[end]);

        // Copy and clear values in vectorization-friendly loops
        for(I jj = start; jj < end; jj++){
            Cx[jj] = sums[Cj[jj]];
        }
        for(I jj = start; jj < end; jj++){
            sums[Cj[jj]] = 0;
        }
    });
}


/*
 * Compute C = A (binary_op) B for CSR matrices that are not
 * necessarily canonical CSR format.  Specifically, this method
 * works even when the input matrices have duplicate and/or
 * unsorted column indices within a given row.
 *
 * Refer to csr_binop_csr() for additional information
 *
 * Note:
 *   Output arrays Cp, Cj, and Cx must be preallocated
 *   If nnz(C) is not known a priori, a conservative bound is:
 *          nnz(C) <= nnz(A) + nnz(B)
 *
 * Note:
 *   Input:  A and B column indices are not assumed to be in sorted order
 *   Output: C column indices are not generally in sorted order
 *           C will not contain any duplicate entries or explicit zeros.
 *
 */
template <class I, class T, class T2, class binary_op>
void csr_binop_csr_general(const I n_row, const I n_col,
                           const I Ap[], const I Aj[], const T Ax[],
                           const I Bp[], const I Bj[], const T Bx[],
                                 I Cp[],       I Cj[],       T2 Cx[],
                           const binary_op& op)
{
    //Method that works for duplicate and/or unsorted indices
    std::vector<I>  next(n_col,-1);
    std::vector<T> A_row(n_col, 0);
    std::vector<T> B_row(n_col, 0);

    I nnz = 0;
    Cp[0] = 0;

    for(I i = 0; i < n_row; i++){
        I head   = -2;
        I length =  0;

        //add a row of A to A_row
        I i_start = Ap[i];
        I i_end   = Ap[i+1];
        for(I jj = i_start; jj < i_end; jj++){
            I j = Aj[jj];

            A_row[j] += Ax[jj];

            if(next[j] == -1){
                next[j] = head;
                head = j;
                length++;
            }
        }

        //add a row of B to B_row
        i_start = Bp[i];
        i_end   = Bp[i+1];
        for(I jj = i_start; jj < i_end; jj++){
            I j = Bj[jj];

            B_row[j] += Bx[jj];

            if(next[j] == -1){
                next[j] = head;
                head = j;
                length++;
            }
        }


        // scan through columns where A or B has
        // contributed a non-zero entry
        for(I jj = 0; jj < length; jj++){
            T result = op(A_row[head], B_row[head]);

            if(result != 0){
                Cj[nnz] = head;
                Cx[nnz] = result;
                nnz++;
            }

            I temp = head;
            head = next[head];

            next[temp]  = -1;
            A_row[temp] =  0;
            B_row[temp] =  0;
        }

        Cp[i + 1] = nnz;
    }
}



/*
 * Compute C = A (binary_op) B for CSR matrices that are in the
 * canonical CSR format.  Specifically, this method requires that
 * the rows of the input matrices are free of duplicate column indices
 * and that the column indices are in sorted order.
 *
 * Refer to csr_binop_csr() for additional information
 *
 * Note:
 *   Input:  A and B column indices are assumed to be in sorted order
 *   Output: C column indices will be in sorted order
 *           Cx will not contain any zero entries
 *
 */
template <class I, class T, class T2, class binary_op>
void csr_binop_csr_canonical(const I n_row, const I n_col,
                             const I Ap[], const I Aj[], const T Ax[],
                             const I Bp[], const I Bj[], const T Bx[],
                                   I Cp[],       I Cj[],       T2 Cx[],
                             const binary_op& op)
{
    //Method that works for canonical CSR matrices

    Cp[0] = 0;
    I nnz = 0;

    for(I i = 0; i < n_row; i++){
        I A_pos = Ap[i];
        I B_pos = Bp[i];
        I A_end = Ap[i+1];
        I B_end = Bp[i+1];

        //while not finished with either row
        while(A_pos < A_end && B_pos < B_end){
            I A_j = Aj[A_pos];
            I B_j = Bj[B_pos];

            if(A_j == B_j){
                T result = op(Ax[A_pos],Bx[B_pos]);
                if(result != 0){
                    Cj[nnz] = A_j;
                    Cx[nnz] = result;
                    nnz++;
                }
                A_pos++;
                B_pos++;
            } else if (A_j < B_j) {
                T result = op(Ax[A_pos],0);
                if (result != 0){
                    Cj[nnz] = A_j;
                    Cx[nnz] = result;
                    nnz++;
                }
                A_pos++;
            } else {
                //B_j < A_j
                T result = op(0,Bx[B_pos]);
                if (result != 0){
                    Cj[nnz] = B_j;
                    Cx[nnz] = result;
                    nnz++;
                }
                B_pos++;
            }
        }

        //tail
        while(A_pos < A_end){
            T result = op(Ax[A_pos],0);
            if (result != 0){
                Cj[nnz] = Aj[A_pos];
                Cx[nnz] = result;
                nnz++;
            }
            A_pos++;
        }
        while(B_pos < B_end){
            T result = op(0,Bx[B_pos]);
            if (result != 0){
                Cj[nnz] = Bj[B_pos];
                Cx[nnz] = result;
                nnz++;
            }
            B_pos++;
        }

        Cp[i+1] = nnz;
    }
}


/*
 * Compute C = A (binary_op) B for CSR matrices A,B where the column
 * indices with the rows of A and B are known to be sorted.
 *
 *   binary_op(x,y) - binary operator to apply elementwise
 *
 * Input Arguments:
 *   I    n_row       - number of rows in A (and B)
 *   I    n_col       - number of columns in A (and B)
 *   I    Ap[n_row+1] - row pointer
 *   I    Aj[nnz(A)]  - column indices
 *   T    Ax[nnz(A)]  - nonzeros
 *   I    Bp[n_row+1] - row pointer
 *   I    Bj[nnz(B)]  - column indices
 *   T    Bx[nnz(B)]  - nonzeros
 * Output Arguments:
 *   I    Cp[n_row+1] - row pointer
 *   I    Cj[nnz(C)]  - column indices
 *   T    Cx[nnz(C)]  - nonzeros
 *
 * Note:
 *   Output arrays Cp, Cj, and Cx must be preallocated
 *   If nnz(C) is not known a priori, a conservative bound is:
 *          nnz(C) <= nnz(A) + nnz(B)
 *
 * Note:
 *   Input:  A and B column indices are not assumed to be in sorted order.
 *   Output: C column indices will be in sorted if both A and B have sorted indices.
 *           Cx will not contain any zero entries
 *
 */
template <class I, class T, class T2, class binary_op>
void csr_binop_csr(const I n_row,
                   const I n_col,
                   const I Ap[],
                   const I Aj[],
                   const T Ax[],
                   const I Bp[],
                   const I Bj[],
                   const T Bx[],
                         I Cp[],
                         I Cj[],
                         T2 Cx[],
                   const binary_op& op)
{
    if (csr_has_canonical_format(n_row,Ap,Aj) && csr_has_canonical_format(n_row,Bp,Bj))
        csr_binop_csr_canonical(n_row, n_col, Ap, Aj, Ax, Bp, Bj, Bx, Cp, Cj, Cx, op);
    else
        csr_binop_csr_general(n_row, n_col, Ap, Aj, Ax, Bp, Bj, Bx, Cp, Cj, Cx, op);
}

/* element-wise binary operations*/
template <class I, class T, class T2>
void csr_ne_csr(const I n_row, const I n_col,
                const I Ap[], const I Aj[], const T Ax[],
                const I Bp[], const I Bj[], const T Bx[],
                      I Cp[],       I Cj[],      T2 Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::not_equal_to<T>());
}

template <class I, class T, class T2>
void csr_lt_csr(const I n_row, const I n_col,
                const I Ap[], const I Aj[], const T Ax[],
                const I Bp[], const I Bj[], const T Bx[],
                      I Cp[],       I Cj[],      T2 Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::less<T>());
}

template <class I, class T, class T2>
void csr_gt_csr(const I n_row, const I n_col,
                const I Ap[], const I Aj[], const T Ax[],
                const I Bp[], const I Bj[], const T Bx[],
                      I Cp[],       I Cj[],      T2 Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::greater<T>());
}

template <class I, class T, class T2>
void csr_le_csr(const I n_row, const I n_col,
                const I Ap[], const I Aj[], const T Ax[],
                const I Bp[], const I Bj[], const T Bx[],
                      I Cp[],       I Cj[],      T2 Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::less_equal<T>());
}

template <class I, class T, class T2>
void csr_ge_csr(const I n_row, const I n_col,
                const I Ap[], const I Aj[], const T Ax[],
                const I Bp[], const I Bj[], const T Bx[],
                      I Cp[],       I Cj[],      T2 Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::greater_equal<T>());
}

template <class I, class T>
void csr_elmul_csr(const I n_row, const I n_col,
                   const I Ap[], const I Aj[], const T Ax[],
                   const I Bp[], const I Bj[], const T Bx[],
                         I Cp[],       I Cj[],       T Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::multiplies<T>());
}

template <class I, class T>
void csr_eldiv_csr(const I n_row, const I n_col,
                   const I Ap[], const I Aj[], const T Ax[],
                   const I Bp[], const I Bj[], const T Bx[],
                         I Cp[],       I Cj[],       T Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,safe_divides<T>());
}


template <class I, class T>
void csr_plus_csr(const I n_row, const I n_col,
                  const I Ap[], const I Aj[], const T Ax[],
                  const I Bp[], const I Bj[], const T Bx[],
                        I Cp[],       I Cj[],       T Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::plus<T>());
}

template <class I, class T>
void csr_minus_csr(const I n_row, const I n_col,
                   const I Ap[], const I Aj[], const T Ax[],
                   const I Bp[], const I Bj[], const T Bx[],
                         I Cp[],       I Cj[],       T Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,std::minus<T>());
}

template <class I, class T>
void csr_maximum_csr(const I n_row, const I n_col,
                     const I Ap[], const I Aj[], const T Ax[],
                     const I Bp[], const I Bj[], const T Bx[],
                           I Cp[],       I Cj[],       T Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,maximum<T>());
}

template <class I, class T>
void csr_minimum_csr(const I n_row, const I n_col,
                     const I Ap[], const I Aj[], const T Ax[],
                     const I Bp[], const I Bj[], const T Bx[],
                           I Cp[],       I Cj[],       T Cx[])
{
    csr_binop_csr(n_row,n_col,Ap,Aj,Ax,Bp,Bj,Bx,Cp,Cj,Cx,minimum<T>());
}


/*
 * Sum together duplicate column entries in each row of CSR matrix A
 *
 *
 * Input Arguments:
 *   I    n_row       - number of rows in A (and B)
 *   I    n_col       - number of columns in A (and B)
 *   I    Ap[n_row+1] - row pointer
 *   I    Aj[nnz(A)]  - column indices
 *   T    Ax[nnz(A)]  - nonzeros
 *
 * Note:
 *   The column indices within each row must be in sorted order.
 *   Explicit zeros are retained.
 *   Ap, Aj, and Ax will be modified *inplace*
 *
 */
template <class I, class T>
void csr_sum_duplicates(const I n_row,
                        const I n_col,
                              I Ap[],
                              I Aj[],
                              T Ax[])
{
    I nnz = 0;
    I row_end = 0;
    for(I i = 0; i < n_row; i++){
        I jj = row_end;
        row_end = Ap[i+1];
        while( jj < row_end ){
            I j = Aj[jj];
            T x = Ax[jj];
            jj++;
            while( jj < row_end && Aj[jj] == j ){
                x += Ax[jj];
                jj++;
            }
            Aj[nnz] = j;
            Ax[nnz] = x;
            nnz++;
        }
        Ap[i+1] = nnz;
    }
}

/*
 * Eliminate zero entries from CSR matrix A
 *
 *
 * Input Arguments:
 *   I    n_row       - number of rows in A (and B)
 *   I    n_col       - number of columns in A (and B)
 *   I    Ap[n_row+1] - row pointer
 *   I    Aj[nnz(A)]  - column indices
 *   T    Ax[nnz(A)]  - nonzeros
 *
 * Note:
 *   Ap, Aj, and Ax will be modified *inplace*
 *
 */
template <class I, class T>
void csr_eliminate_zeros(const I n_row,
                         const I n_col,
                               I Ap[],
                               I Aj[],
                               T Ax[])
{
    I nnz = 0;
    I row_end = 0;
    for(I i = 0; i < n_row; i++){
        I jj = row_end;
        row_end = Ap[i+1];
        while( jj < row_end ){
            I j = Aj[jj];
            T x = Ax[jj];
            if(x != 0){
                Aj[nnz] = j;
                Ax[nnz] = x;
                nnz++;
            }
            jj++;
        }
        Ap[i+1] = nnz;
    }
}



/*
 * Compute Y += A*X for CSR matrix A and dense vectors X,Y
 *
 *
 * Input Arguments:
 *   I  n_row         - number of rows in A
 *   I  n_col         - number of columns in A
 *   I  Ap[n_row+1]   - row pointer
 *   I  Aj[nnz(A)]    - column indices
 *   T  Ax[nnz(A)]    - nonzeros
 *   T  Xx[n_col]     - input vector
 *
 * Output Arguments:
 *   T  Yx[n_row]     - output vector
 *
 * Note:
 *   Output array Yx must be preallocated
 *
 *   Complexity: Linear.  Specifically O(nnz(A) + n_row)
 *
 */
template <class I, class T>
void csr_matvec(const I n_row,
                const I n_col,
                const I Ap[],
                const I Aj[],
                const T Ax[],
                const T Xx[],
                      T Yx[])
{
    std::for_each(workers.par_if_flops(Ap[n_row]),
                  iota_iter<I>(0), iota_iter<I>(n_row), [&](I i){
        T sum = Yx[i];
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            sum += Ax[jj] * Xx[Aj[jj]];
        }
        Yx[i] = sum;
    });
}


/*
 * Compute Y += A*X for CSR matrix A and dense block vectors X,Y
 *
 *
 * Input Arguments:
 *   I  n_row            - number of rows in A
 *   I  n_col            - number of columns in A
 *   I  n_vecs           - number of column vectors in X and Y
 *   I  Ap[n_row+1]      - row pointer
 *   I  Aj[nnz(A)]       - column indices
 *   T  Ax[nnz(A)]       - nonzeros
 *   T  Xx[n_col,n_vecs] - input vector
 *
 * Output Arguments:
 *   T  Yx[n_row,n_vecs] - output vector
 *
 */
template <class I, class T>
void csr_matvecs(const I n_row,
                 const I n_col,
                 const I n_vecs,
                 const I Ap[],
                 const I Aj[],
                 const T Ax[],
                 const T Xx[],
                       T Yx[])
{
    for(I i = 0; i < n_row; i++){
        T * y = Yx + (npy_intp)n_vecs * i;
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            const I j = Aj[jj];
            const T a = Ax[jj];
            const T * x = Xx + (npy_intp)n_vecs * j;
            axpy(n_vecs, a, x, y);
        }
    }
}




template<class I, class T>
void get_csr_submatrix(const I n_row,
                       const I n_col,
                       const I Ap[],
                       const I Aj[],
                       const T Ax[],
                       const I ir0,
                       const I ir1,
                       const I ic0,
                       const I ic1,
                       std::vector<I>* Bp,
                       std::vector<I>* Bj,
                       std::vector<T>* Bx)
{
    I new_n_row = ir1 - ir0;
    //I new_n_col = ic1 - ic0;  //currently unused
    I new_nnz = 0;
    I kk = 0;

    // Count nonzeros total/per row.
    for(I i = 0; i < new_n_row; i++){
        I row_start = Ap[ir0+i];
        I row_end   = Ap[ir0+i+1];

        for(I jj = row_start; jj < row_end; jj++){
            if ((Aj[jj] >= ic0) && (Aj[jj] < ic1)) {
                new_nnz++;
            }
        }
    }

    // Allocate.
    Bp->resize(new_n_row+1);
    Bj->resize(new_nnz);
    Bx->resize(new_nnz);

    // Assign.
    (*Bp)[0] = 0;
    for(I i = 0; i < new_n_row; i++){
        I row_start = Ap[ir0+i];
        I row_end   = Ap[ir0+i+1];

        for(I jj = row_start; jj < row_end; jj++){
            if ((Aj[jj] >= ic0) && (Aj[jj] < ic1)) {
                (*Bj)[kk] = Aj[jj] - ic0;
                (*Bx)[kk] = Ax[jj];
                kk++;
            }
        }
        (*Bp)[i+1] = kk;
    }
}


/*
 * Slice rows given as an array of indices.
 *
 * Input Arguments:
 *   I  n_row_idx       - number of row indices
 *   I  rows[n_row_idx] - row indices for indexing
 *   I  Ap[n_row+1]     - row pointer
 *   I  Aj[nnz(A)]      - column indices
 *   T  Ax[nnz(A)]      - data
 *
 * Output Arguments:
 *   I  Bj - new column indices
 *   T  Bx - new data
 *
 */
template<class I, class T>
void csr_row_index(const I n_row_idx,
                   const I rows[],
                   const I Ap[],
                   const I Aj[],
                   const T Ax[],
                   I Bj[],
                   T Bx[])
{
    for(I i = 0; i < n_row_idx; i++){
        const I row = rows[i];
        const I row_start = Ap[row];
        const I row_end   = Ap[row+1];
        Bj = std::copy(Aj + row_start, Aj + row_end, Bj);
        Bx = std::copy(Ax + row_start, Ax + row_end, Bx);
    }
}


/*
 * Slice rows given as a (start, stop, step) tuple.
 *
 * Input Arguments:
 *   I  start
 *   I  stop
 *   I  step
 *   I  Ap[N+1]    - row pointer
 *   I  Aj[nnz(A)] - column indices
 *   T  Ax[nnz(A)] - data
 *
 * Output Arguments:
 *   I  Bj - new column indices
 *   T  Bx - new data
 *
 */
template<class I, class T>
void csr_row_slice(const I start,
                   const I stop,
                   const I step,
                   const I Ap[],
                   const I Aj[],
                   const T Ax[],
                   I Bj[],
                   T Bx[])
{
    if (step > 0) {
        for(I row = start; row < stop; row += step){
            const I row_start = Ap[row];
            const I row_end   = Ap[row+1];
            Bj = std::copy(Aj + row_start, Aj + row_end, Bj);
            Bx = std::copy(Ax + row_start, Ax + row_end, Bx);
        }
    } else {
        for(I row = start; row > stop; row += step){
            const I row_start = Ap[row];
            const I row_end   = Ap[row+1];
            Bj = std::copy(Aj + row_start, Aj + row_end, Bj);
            Bx = std::copy(Ax + row_start, Ax + row_end, Bx);
        }
    }
}


/*
 * Slice columns given as an array of indices (pass 1).
 * This pass counts idx entries and computes a new indptr.
 *
 * Input Arguments:
 *   I  n_idx           - number of indices to slice
 *   I  col_idxs[n_idx] - indices to slice
 *   I  n_row           - major axis dimension
 *   I  n_col           - minor axis dimension
 *   I  Ap[n_row+1]     - indptr
 *   I  Aj[nnz(A)]      - indices
 *
 * Output Arguments:
 *   I  col_offsets[n_col] - cumsum of index repeats
 *   I  Bp[n_row+1]        - new indptr
 *
 */
template<class I>
void csr_column_index1(const I n_idx,
                       const I col_idxs[],
                       const I n_row,
                       const I n_col,
                       const I Ap[],
                       const I Aj[],
                       I col_offsets[],
                       I Bp[])
{
    // bincount(col_idxs)
    for(I jj = 0; jj < n_idx; jj++){
        const I j = col_idxs[jj];
        col_offsets[j]++;
    }

    // Compute new indptr
    I new_nnz = 0;
    Bp[0] = 0;
    for(I i = 0; i < n_row; i++){
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            new_nnz += col_offsets[Aj[jj]];
        }
        Bp[i+1] = new_nnz;
    }

    // cumsum in-place
    for(I j = 1; j < n_col; j++){
        col_offsets[j] += col_offsets[j - 1];
    }
}


/*
 * Slice columns given as an array of indices (pass 2).
 * This pass populates indices/data entries for selected columns.
 *
 * Input Arguments:
 *   I  col_order[n_idx]   - order of col indices
 *   I  col_offsets[n_col] - cumsum of col index counts
 *   I  nnz                - nnz(A)
 *   I  Aj[nnz(A)]         - column indices
 *   T  Ax[nnz(A)]         - data
 *
 * Output Arguments:
 *   I  Bj[nnz(B)] - new column indices
 *   T  Bx[nnz(B)] - new data
 *
 */
template<class I, class T>
void csr_column_index2(const I col_order[],
                       const I col_offsets[],
                       const I nnz,
                       const I Aj[],
                       const T Ax[],
                       I Bj[],
                       T Bx[])
{
    I n = 0;
    for(I jj = 0; jj < nnz; jj++){
        const I j = Aj[jj];
        const I offset = col_offsets[j];
        const I prev_offset = j == 0 ? 0 : col_offsets[j-1];
        if (offset != prev_offset) {
            const T v = Ax[jj];
            for(I k = prev_offset; k < offset; k++){
                Bj[n] = col_order[k];
                Bx[n] = v;
                n++;
            }
        }
    }
}


/*
 * Count the number of occupied diagonals in CSR matrix A
 *
 * Input Arguments:
 *   I  nnz             - number of nonzeros in A
 *   I  Ai[nnz(A)]      - row indices
 *   I  Aj[nnz(A)]      - column indices
 *
 */
template <class I>
I csr_count_diagonals(const I n_row,
                      const I Ap[],
                      const I Aj[])
{
    std::set<I> diagonals;

    for(I i = 0; i < n_row; i++){
        for(I jj = Ap[i]; jj < Ap[i+1]; jj++){
            diagonals.insert(Aj[jj] - i);
        }
    }
    return diagonals.size();
}


/*
 * Sample the matrix at specific locations
 *
 * Determine the matrix value for each row,col pair
 *    Bx[n] = A(Bi[n],Bj[n])
 *
 * Input Arguments:
 *   I  n_row         - number of rows in A
 *   I  n_col         - number of columns in A
 *   I  Ap[n_row+1]   - row pointer
 *   I  Aj[nnz(A)]    - column indices
 *   T  Ax[nnz(A)]    - nonzeros
 *   I  n_samples     - number of samples
 *   I  Bi[N]         - sample rows
 *   I  Bj[N]         - sample columns
 *
 * Output Arguments:
 *   T  Bx[N]         - sample values
 *
 * Note:
 *   Output array Bx must be preallocated
 *
 *   Complexity: varies
 *
 *   TODO handle other cases with asymptotically optimal method
 *
 */
template <class I, class T>
void csr_sample_values(const I n_row,
                       const I n_col,
                       const I Ap[],
                       const I Aj[],
                       const T Ax[],
                       const I n_samples,
                       const I Bi[],
                       const I Bj[],
                             T Bx[])
{
    // ideally we'd do the following
    // Case 1: A is canonical and B is sorted by row and column
    //   -> special purpose csr_binop_csr() (optimized form)
    // Case 2: A is canonical and B is unsorted and max(log(Ap[i+1] - Ap[i])) > log(num_samples)
    //   -> do binary searches for each sample
    // Case 3: A is canonical and B is unsorted and max(log(Ap[i+1] - Ap[i])) < log(num_samples)
    //   -> sort B by row and column and use Case 1
    // Case 4: A is not canonical and num_samples ~ nnz
    //   -> special purpose csr_binop_csr() (general form)
    // Case 5: A is not canonical and num_samples << nnz
    //   -> do linear searches for each sample

    const I nnz = Ap[n_row];

    const I threshold = nnz / 10; // constant is arbitrary

    if (n_samples > threshold && csr_has_canonical_format(n_row, Ap, Aj))
    {
        for(I n = 0; n < n_samples; n++)
        {
            const I i = Bi[n] < 0 ? Bi[n] + n_row : Bi[n]; // sample row
            const I j = Bj[n] < 0 ? Bj[n] + n_col : Bj[n]; // sample column

            const I row_start = Ap[i];
            const I row_end   = Ap[i+1];

            if (row_start < row_end)
            {
                const I offset = std::lower_bound(Aj + row_start, Aj + row_end, j) - Aj;

                if (offset < row_end && Aj[offset] == j)
                    Bx[n] = Ax[offset];
                else
                    Bx[n] = 0;
            }
            else
            {
                Bx[n] = 0;
            }

        }
    }
    else
    {
        for(I n = 0; n < n_samples; n++)
        {
            const I i = Bi[n] < 0 ? Bi[n] + n_row : Bi[n]; // sample row
            const I j = Bj[n] < 0 ? Bj[n] + n_col : Bj[n]; // sample column

            const I row_start = Ap[i];
            const I row_end   = Ap[i+1];

            T x = 0;

            for(I jj = row_start; jj < row_end; jj++)
            {
                if (Aj[jj] == j)
                    x += Ax[jj];
            }

            Bx[n] = x;
        }

    }
}

/*
 * Determine the data offset at specific locations
 *
 * Input Arguments:
 *   I  n_row         - number of rows in A
 *   I  n_col         - number of columns in A
 *   I  Ap[n_row+1]   - row pointer
 *   I  Aj[nnz(A)]    - column indices
 *   I  n_samples     - number of samples
 *   I  Bi[N]         - sample rows
 *   I  Bj[N]         - sample columns
 *
 * Output Arguments:
 *   I  Bp[N]         - offsets into Aj; -1 if non-existent
 *
 * Return value:
 *   1 if any sought entries are duplicated, in which case the
 *   function has exited early; 0 otherwise.
 *
 * Note:
 *   Output array Bp must be preallocated
 *
 *   Complexity: varies. See csr_sample_values
 *
 */
template <class I>
int csr_sample_offsets(const I n_row,
                       const I n_col,
                       const I Ap[],
                       const I Aj[],
                       const I n_samples,
                       const I Bi[],
                       const I Bj[],
                             I Bp[])
{
    const I nnz = Ap[n_row];
    const I threshold = nnz / 10; // constant is arbitrary

    if (n_samples > threshold && csr_has_canonical_format(n_row, Ap, Aj))
    {
        for(I n = 0; n < n_samples; n++)
        {
            const I i = Bi[n] < 0 ? Bi[n] + n_row : Bi[n]; // sample row
            const I j = Bj[n] < 0 ? Bj[n] + n_col : Bj[n]; // sample column

            const I row_start = Ap[i];
            const I row_end   = Ap[i+1];

            if (row_start < row_end)
            {
                const I offset = std::lower_bound(Aj + row_start, Aj + row_end, j) - Aj;

                if (offset < row_end && Aj[offset] == j)
                    Bp[n] = offset;
                else
                    Bp[n] = -1;
            }
            else
            {
                Bp[n] = -1;
            }
        }
    }
    else
    {
        for(I n = 0; n < n_samples; n++)
        {
            const I i = Bi[n] < 0 ? Bi[n] + n_row : Bi[n]; // sample row
            const I j = Bj[n] < 0 ? Bj[n] + n_col : Bj[n]; // sample column

            const I row_start = Ap[i];
            const I row_end   = Ap[i+1];

            I offset = -1;

            for(I jj = row_start; jj < row_end; jj++)
            {
                if (Aj[jj] == j) {
                    offset = jj;
                    for (jj++; jj < row_end; jj++) {
                        if (Aj[jj] == j) {
                            offset = -2;
                            return 1;
                        }
                    }
                }
            }
            Bp[n] = offset;
        }
    }
    return 0;
}

/*
 * Stack CSR matrices in A horizontally (column wise)
 *
 * Input Arguments:
 *   I  n_blocks                      - number of matrices in A
 *   I  n_row                         - number of rows in any matrix in A
 *   I  n_col_cat[n_blocks]           - number of columns in each matrix in A concatenated
 *   I  Ap_cat[n_blocks*(n_row + 1)]  - row indices of each matrix in A concatenated
 *   I  Aj_cat[nnz(A)]                - column indices of each matrix in A concatenated
 *   T  Ax_cat[nnz(A)]                - nonzeros of each matrix in A concatenated
 *
 * Output Arguments:
 *   I Bp  - row pointer
 *   I Bj  - column indices
 *   T Bx  - nonzeros
 *
 * Note:
 *   All output arrays Bp, Bj, Bx must be preallocated
 *
 *   Complexity: Linear.  Specifically O(nnz(A) + n_blocks)
 *
 */
template <class I, class T>
void csr_hstack(const I n_blocks,
                const I n_row,
                const I n_col_cat[],
                const I Ap_cat[],
                const I Aj_cat[],
                const T Ax_cat[],
                      I Bp[],
                      I Bj[],
                      T Bx[])
{
    // First, mark the blocks in the input data while
    // computing their column offsets:
    std::vector<I> col_offset(n_blocks);
    std::vector<const I*> bAp(n_blocks);
    std::vector<const I*> bAj(n_blocks);
    std::vector<const T*> bAx(n_blocks);
    col_offset[0] = 0;
    bAp[0] = Ap_cat;
    bAj[0] = Aj_cat;
    bAx[0] = Ax_cat;
    for (I b = 1; b < n_blocks; b++){
        col_offset[b] = col_offset[b - 1] + n_col_cat[b - 1];
        bAp[b] = bAp[b - 1] + (n_row + 1);
        bAj[b] = bAj[b - 1] + bAp[b - 1][n_row];
        bAx[b] = bAx[b - 1] + bAp[b - 1][n_row];
    }

    // Next, build the full output matrix:
    Bp[0] = 0;
    I s = 0;
    for(I i = 0; i < n_row; i++){
        for (I b = 0; b < n_blocks; b++){
            I jj_start = bAp[b][i];
            I jj_end = bAp[b][i + 1];
            I offset = col_offset[b];
            std::transform(&bAj[b][jj_start], &bAj[b][jj_end],
                           &Bj[s], [&](I x){return (x + offset);});
            std::copy(&bAx[b][jj_start], &bAx[b][jj_end], &Bx[s]);
            s += jj_end - jj_start;
        }
        Bp[i + 1] = s;
    }

}


/*
 * A test function checking the error handling
 */
inline int test_throw_error() {
    throw std::bad_alloc();
    return 1;
}

inline int get_workers() {
    return workers.get_workers();
}

inline void set_workers(int desired_workers) {
    workers.set_workers(desired_workers);
}

inline void set_par_threshold(int threshold) {
    workers.set_par_threshold(threshold);
}

#define SPTOOLS_CSR_EXTERN_TEMPLATE(I, T) \
  extern template void csr_diagonal(const I k, const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], T Yx[]); \
  extern template void csr_scale_rows(const I n_row, const I n_col, const I Ap[], const I Aj[], T Ax[], const T Xx[]); \
  extern template void csr_scale_columns(const I n_row, const I n_col, const I Ap[], const I Aj[], T Ax[], const T Xx[]); \
  extern template void csr_tobsr(const I n_row, const I n_col, const I R, const I C, const I Ap[], const I Aj[], const T Ax[], I Bp[], I Bj[], T Bx[]); \
  extern template void csr_todense(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], T Bx[]); \
  extern template void csr_sort_indices(const I n_row, const I Ap[], I Aj[], T Ax[]); \
  extern template void csr_tocsc(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], I Bp[], I Bi[], T Bx[]); \
  extern template void csr_toell(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I row_length, I Bj[], T Bx[]); \
  extern template void csr_matmat(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], const I Cp[], I Cj[], T Cx[]); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const std::not_equal_to<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const std::less<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const std::less_equal<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const std::greater_equal<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const std::multiplies<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const safe_divides<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const std::plus<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const std::minus<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const maximum<T>& op); \
  extern template void csr_binop_csr(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I Bp[], const I Bj[], const T Bx[], I Cp[], I Cj[], T Cx[], const minimum<T>& op); \
  extern template void csr_sum_duplicates(const I n_row, const I n_col, I Ap[], I Aj[], T Ax[]); \
  extern template void csr_eliminate_zeros(const I n_row, const I n_col, I Ap[], I Aj[], T Ax[]); \
  extern template void csr_matvec(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const T Xx[], T Yx[]); \
  extern template void csr_matvecs(const I n_row, const I n_col, const I n_vecs, const I Ap[], const I Aj[], const T Ax[], const T Xx[], T Yx[]); \
  extern template void get_csr_submatrix(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I ir0, const I ir1, const I ic0, const I ic1, std::vector<I>* Bp, std::vector<I>* Bj, std::vector<T>* Bx); \
  extern template void csr_row_index(const I n_row_idx, const I rows[], const I Ap[], const I Aj[], const T Ax[], I Bj[], T Bx[]); \
  extern template void csr_row_slice(const I start, const I stop, const I step, const I Ap[], const I Aj[], const T Ax[], I Bj[], T Bx[]); \
  extern template void csr_column_index2(const I col_order[], const I col_offsets[], const I nnz, const I Aj[], const T Ax[], I Bj[], T Bx[]); \
  extern template void csr_sample_values(const I n_row, const I n_col, const I Ap[], const I Aj[], const T Ax[], const I n_samples, const I Bi[], const I Bj[], T Bx[]);

SPTOOLS_FOR_EACH_INDEX_DATA_TYPE_COMBINATION(SPTOOLS_CSR_EXTERN_TEMPLATE)
#undef SPTOOLS_CSR_EXTERN_TEMPLATE

#endif
