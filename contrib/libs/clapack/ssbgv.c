/* ssbgv.f -- translated by f2c (version 20061008).
   You must link the resulting object file with libf2c:
	on Microsoft Windows system, link with libf2c.lib;
	on Linux or Unix systems, link with .../path/to/libf2c.a -lm
	or, if you install libf2c.a in a standard place, with -lf2c -lm
	-- in that order, at the end of the command line, as in
		cc *.o -lf2c -lm
	Source for libf2c is in /netlib/f2c/libf2c.zip, e.g.,

		http://www.netlib.org/f2c/libf2c.zip
*/

#include "f2c.h"
#include "blaswrap.h"

/* Subroutine */ int ssbgv_(char *jobz, char *uplo, integer *n, integer *ka, 
	integer *kb, real *ab, integer *ldab, real *bb, integer *ldbb, real *
	w, real *z__, integer *ldz, real *work, integer *info)
{
    /* System generated locals */
    integer ab_dim1, ab_offset, bb_dim1, bb_offset, z_dim1, z_offset, i__1;

    /* Local variables */
    integer inde;
    char vect[1];
    extern logical lsame_(char *, char *);
    integer iinfo;
    logical upper, wantz;
    extern /* Subroutine */ int xerbla_(char *, integer *);
    integer indwrk;
    extern /* Subroutine */ int spbstf_(char *, integer *, integer *, real *, 
	    integer *, integer *), ssbtrd_(char *, char *, integer *, 
	    integer *, real *, integer *, real *, real *, real *, integer *, 
	    real *, integer *), ssbgst_(char *, char *, 
	    integer *, integer *, integer *, real *, integer *, real *, 
	    integer *, real *, integer *, real *, integer *), 
	    ssterf_(integer *, real *, real *, integer *), ssteqr_(char *, 
	    integer *, real *, real *, real *, integer *, real *, integer *);


/*  -- LAPACK driver routine (version 3.2) -- */
/*     Univ. of Tennessee, Univ. of California Berkeley and NAG Ltd.. */
/*     November 2006 */

/*     .. Scalar Arguments .. */
/*     .. */
/*     .. Array Arguments .. */
/*     .. */

/*  Purpose */
/*  ======= */

/*  SSBGV computes all the eigenvalues, and optionally, the eigenvectors */
/*  of a real generalized symmetric-definite banded eigenproblem, of */
/*  the form A*x=(lambda)*B*x. Here A and B are assumed to be symmetric */
/*  and banded, and B is also positive definite. */

/*  Arguments */
/*  ========= */

/*  JOBZ    (input) CHARACTER*1 */
/*          = 'N':  Compute eigenvalues only; */
/*          = 'V':  Compute eigenvalues and eigenvectors. */

/*  UPLO    (input) CHARACTER*1 */
/*          = 'U':  Upper triangles of A and B are stored; */
/*          = 'L':  Lower triangles of A and B are stored. */

/*  N       (input) INTEGER */
/*          The order of the matrices A and B.  N >= 0. */

/*  KA      (input) INTEGER */
/*          The number of superdiagonals of the matrix A if UPLO = 'U', */
/*          or the number of subdiagonals if UPLO = 'L'. KA >= 0. */

/*  KB      (input) INTEGER */
/*          The number of superdiagonals of the matrix B if UPLO = 'U', */
/*          or the number of subdiagonals if UPLO = 'L'. KB >= 0. */

/*  AB      (input/output) REAL array, dimension (LDAB, N) */
/*          On entry, the upper or lower triangle of the symmetric band */
/*          matrix A, stored in the first ka+1 rows of the array.  The */
/*          j-th column of A is stored in the j-th column of the array AB */
/*          as follows: */
/*          if UPLO = 'U', AB(ka+1+i-j,j) = A(i,j) for max(1,j-ka)<=i<=j; */
/*          if UPLO = 'L', AB(1+i-j,j)    = A(i,j) for j<=i<=min(n,j+ka). */

/*          On exit, the contents of AB are destroyed. */

/*  LDAB    (input) INTEGER */
/*          The leading dimension of the array AB.  LDAB >= KA+1. */

/*  BB      (input/output) REAL array, dimension (LDBB, N) */
/*          On entry, the upper or lower triangle of the symmetric band */
/*          matrix B, stored in the first kb+1 rows of the array.  The */
/*          j-th column of B is stored in the j-th column of the array BB */
/*          as follows: */
/*          if UPLO = 'U', BB(kb+1+i-j,j) = B(i,j) for max(1,j-kb)<=i<=j; */
/*          if UPLO = 'L', BB(1+i-j,j)    = B(i,j) for j<=i<=min(n,j+kb). */

/*          On exit, the factor S from the split Cholesky factorization */
/*          B = S**T*S, as returned by SPBSTF. */

/*  LDBB    (input) INTEGER */
/*          The leading dimension of the array BB.  LDBB >= KB+1. */

/*  W       (output) REAL array, dimension (N) */
/*          If INFO = 0, the eigenvalues in ascending order. */

/*  Z       (output) REAL array, dimension (LDZ, N) */
/*          If JOBZ = 'V', then if INFO = 0, Z contains the matrix Z of */
/*          eigenvectors, with the i-th column of Z holding the */
/*          eigenvector associated with W(i). The eigenvectors are */
/*          normalized so that Z**T*B*Z = I. */
/*          If JOBZ = 'N', then Z is not referenced. */

/*  LDZ     (input) INTEGER */
/*          The leading dimension of the array Z.  LDZ >= 1, and if */
/*          JOBZ = 'V', LDZ >= N. */

/*  WORK    (workspace) REAL array, dimension (3*N) */

/*  INFO    (output) INTEGER */
/*          = 0:  successful exit */
/*          < 0:  if INFO = -i, the i-th argument had an illegal value */
/*          > 0:  if INFO = i, and i is: */
/*             <= N:  the algorithm failed to converge: */
/*                    i off-diagonal elements of an intermediate */
/*                    tridiagonal form did not converge to zero; */
/*             > N:   if INFO = N + i, for 1 <= i <= N, then SPBSTF */
/*                    returned INFO = i: B is not positive definite. */
/*                    The factorization of B could not be completed and */
/*                    no eigenvalues or eigenvectors were computed. */

/*  ===================================================================== */

/*     .. Local Scalars .. */
/*     .. */
/*     .. External Functions .. */
/*     .. */
/*     .. External Subroutines .. */
/*     .. */
/*     .. Executable Statements .. */

/*     Test the input parameters. */

    /* Parameter adjustments */
    ab_dim1 = *ldab;
    ab_offset = 1 + ab_dim1;
    ab -= ab_offset;
    bb_dim1 = *ldbb;
    bb_offset = 1 + bb_dim1;
    bb -= bb_offset;
    --w;
    z_dim1 = *ldz;
    z_offset = 1 + z_dim1;
    z__ -= z_offset;
    --work;

    /* Function Body */
    wantz = lsame_(jobz, "V");
    upper = lsame_(uplo, "U");

    *info = 0;
    if (! (wantz || lsame_(jobz, "N"))) {
	*info = -1;
    } else if (! (upper || lsame_(uplo, "L"))) {
	*info = -2;
    } else if (*n < 0) {
	*info = -3;
    } else if (*ka < 0) {
	*info = -4;
    } else if (*kb < 0 || *kb > *ka) {
	*info = -5;
    } else if (*ldab < *ka + 1) {
	*info = -7;
    } else if (*ldbb < *kb + 1) {
	*info = -9;
    } else if (*ldz < 1 || wantz && *ldz < *n) {
	*info = -12;
    }
    if (*info != 0) {
	i__1 = -(*info);
	xerbla_("SSBGV ", &i__1);
	return 0;
    }

/*     Quick return if possible */

    if (*n == 0) {
	return 0;
    }

/*     Form a split Cholesky factorization of B. */

    spbstf_(uplo, n, kb, &bb[bb_offset], ldbb, info);
    if (*info != 0) {
	*info = *n + *info;
	return 0;
    }

/*     Transform problem to standard eigenvalue problem. */

    inde = 1;
    indwrk = inde + *n;
    ssbgst_(jobz, uplo, n, ka, kb, &ab[ab_offset], ldab, &bb[bb_offset], ldbb, 
	     &z__[z_offset], ldz, &work[indwrk], &iinfo)
	    ;

/*     Reduce to tridiagonal form. */

    if (wantz) {
	*(unsigned char *)vect = 'U';
    } else {
	*(unsigned char *)vect = 'N';
    }
    ssbtrd_(vect, uplo, n, ka, &ab[ab_offset], ldab, &w[1], &work[inde], &z__[
	    z_offset], ldz, &work[indwrk], &iinfo);

/*     For eigenvalues only, call SSTERF.  For eigenvectors, call SSTEQR. */

    if (! wantz) {
	ssterf_(n, &w[1], &work[inde], info);
    } else {
	ssteqr_(jobz, n, &w[1], &work[inde], &z__[z_offset], ldz, &work[
		indwrk], info);
    }
    return 0;

/*     End of SSBGV */

} /* ssbgv_ */
