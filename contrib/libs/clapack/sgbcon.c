/* sgbcon.f -- translated by f2c (version 20061008).
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

/* Table of constant values */

static integer c__1 = 1;

/* Subroutine */ int sgbcon_(char *norm, integer *n, integer *kl, integer *ku, 
	 real *ab, integer *ldab, integer *ipiv, real *anorm, real *rcond, 
	real *work, integer *iwork, integer *info)
{
    /* System generated locals */
    integer ab_dim1, ab_offset, i__1, i__2, i__3;
    real r__1;

    /* Local variables */
    integer j;
    real t;
    integer kd, lm, jp, ix, kase;
    extern doublereal sdot_(integer *, real *, integer *, real *, integer *);
    integer kase1;
    real scale;
    extern logical lsame_(char *, char *);
    integer isave[3];
    logical lnoti;
    extern /* Subroutine */ int srscl_(integer *, real *, real *, integer *), 
	    saxpy_(integer *, real *, real *, integer *, real *, integer *), 
	    slacn2_(integer *, real *, real *, integer *, real *, integer *, 
	    integer *);
    extern doublereal slamch_(char *);
    extern /* Subroutine */ int xerbla_(char *, integer *);
    extern integer isamax_(integer *, real *, integer *);
    real ainvnm;
    extern /* Subroutine */ int slatbs_(char *, char *, char *, char *, 
	    integer *, integer *, real *, integer *, real *, real *, real *, 
	    integer *);
    logical onenrm;
    char normin[1];
    real smlnum;


/*  -- LAPACK routine (version 3.2) -- */
/*     Univ. of Tennessee, Univ. of California Berkeley and NAG Ltd.. */
/*     November 2006 */

/*     Modified to call SLACN2 in place of SLACON, 7 Feb 03, SJH. */

/*     .. Scalar Arguments .. */
/*     .. */
/*     .. Array Arguments .. */
/*     .. */

/*  Purpose */
/*  ======= */

/*  SGBCON estimates the reciprocal of the condition number of a real */
/*  general band matrix A, in either the 1-norm or the infinity-norm, */
/*  using the LU factorization computed by SGBTRF. */

/*  An estimate is obtained for norm(inv(A)), and the reciprocal of the */
/*  condition number is computed as */
/*     RCOND = 1 / ( norm(A) * norm(inv(A)) ). */

/*  Arguments */
/*  ========= */

/*  NORM    (input) CHARACTER*1 */
/*          Specifies whether the 1-norm condition number or the */
/*          infinity-norm condition number is required: */
/*          = '1' or 'O':  1-norm; */
/*          = 'I':         Infinity-norm. */

/*  N       (input) INTEGER */
/*          The order of the matrix A.  N >= 0. */

/*  KL      (input) INTEGER */
/*          The number of subdiagonals within the band of A.  KL >= 0. */

/*  KU      (input) INTEGER */
/*          The number of superdiagonals within the band of A.  KU >= 0. */

/*  AB      (input) REAL array, dimension (LDAB,N) */
/*          Details of the LU factorization of the band matrix A, as */
/*          computed by SGBTRF.  U is stored as an upper triangular band */
/*          matrix with KL+KU superdiagonals in rows 1 to KL+KU+1, and */
/*          the multipliers used during the factorization are stored in */
/*          rows KL+KU+2 to 2*KL+KU+1. */

/*  LDAB    (input) INTEGER */
/*          The leading dimension of the array AB.  LDAB >= 2*KL+KU+1. */

/*  IPIV    (input) INTEGER array, dimension (N) */
/*          The pivot indices; for 1 <= i <= N, row i of the matrix was */
/*          interchanged with row IPIV(i). */

/*  ANORM   (input) REAL */
/*          If NORM = '1' or 'O', the 1-norm of the original matrix A. */
/*          If NORM = 'I', the infinity-norm of the original matrix A. */

/*  RCOND   (output) REAL */
/*          The reciprocal of the condition number of the matrix A, */
/*          computed as RCOND = 1/(norm(A) * norm(inv(A))). */

/*  WORK    (workspace) REAL array, dimension (3*N) */

/*  IWORK   (workspace) INTEGER array, dimension (N) */

/*  INFO    (output) INTEGER */
/*          = 0:  successful exit */
/*          < 0: if INFO = -i, the i-th argument had an illegal value */

/*  ===================================================================== */

/*     .. Parameters .. */
/*     .. */
/*     .. Local Scalars .. */
/*     .. */
/*     .. Local Arrays .. */
/*     .. */
/*     .. External Functions .. */
/*     .. */
/*     .. External Subroutines .. */
/*     .. */
/*     .. Intrinsic Functions .. */
/*     .. */
/*     .. Executable Statements .. */

/*     Test the input parameters. */

    /* Parameter adjustments */
    ab_dim1 = *ldab;
    ab_offset = 1 + ab_dim1;
    ab -= ab_offset;
    --ipiv;
    --work;
    --iwork;

    /* Function Body */
    *info = 0;
    onenrm = *(unsigned char *)norm == '1' || lsame_(norm, "O");
    if (! onenrm && ! lsame_(norm, "I")) {
	*info = -1;
    } else if (*n < 0) {
	*info = -2;
    } else if (*kl < 0) {
	*info = -3;
    } else if (*ku < 0) {
	*info = -4;
    } else if (*ldab < (*kl << 1) + *ku + 1) {
	*info = -6;
    } else if (*anorm < 0.f) {
	*info = -8;
    }
    if (*info != 0) {
	i__1 = -(*info);
	xerbla_("SGBCON", &i__1);
	return 0;
    }

/*     Quick return if possible */

    *rcond = 0.f;
    if (*n == 0) {
	*rcond = 1.f;
	return 0;
    } else if (*anorm == 0.f) {
	return 0;
    }

    smlnum = slamch_("Safe minimum");

/*     Estimate the norm of inv(A). */

    ainvnm = 0.f;
    *(unsigned char *)normin = 'N';
    if (onenrm) {
	kase1 = 1;
    } else {
	kase1 = 2;
    }
    kd = *kl + *ku + 1;
    lnoti = *kl > 0;
    kase = 0;
L10:
    slacn2_(n, &work[*n + 1], &work[1], &iwork[1], &ainvnm, &kase, isave);
    if (kase != 0) {
	if (kase == kase1) {

/*           Multiply by inv(L). */

	    if (lnoti) {
		i__1 = *n - 1;
		for (j = 1; j <= i__1; ++j) {
/* Computing MIN */
		    i__2 = *kl, i__3 = *n - j;
		    lm = min(i__2,i__3);
		    jp = ipiv[j];
		    t = work[jp];
		    if (jp != j) {
			work[jp] = work[j];
			work[j] = t;
		    }
		    r__1 = -t;
		    saxpy_(&lm, &r__1, &ab[kd + 1 + j * ab_dim1], &c__1, &
			    work[j + 1], &c__1);
/* L20: */
		}
	    }

/*           Multiply by inv(U). */

	    i__1 = *kl + *ku;
	    slatbs_("Upper", "No transpose", "Non-unit", normin, n, &i__1, &
		    ab[ab_offset], ldab, &work[1], &scale, &work[(*n << 1) + 
		    1], info);
	} else {

/*           Multiply by inv(U'). */

	    i__1 = *kl + *ku;
	    slatbs_("Upper", "Transpose", "Non-unit", normin, n, &i__1, &ab[
		    ab_offset], ldab, &work[1], &scale, &work[(*n << 1) + 1], 
		    info);

/*           Multiply by inv(L'). */

	    if (lnoti) {
		for (j = *n - 1; j >= 1; --j) {
/* Computing MIN */
		    i__1 = *kl, i__2 = *n - j;
		    lm = min(i__1,i__2);
		    work[j] -= sdot_(&lm, &ab[kd + 1 + j * ab_dim1], &c__1, &
			    work[j + 1], &c__1);
		    jp = ipiv[j];
		    if (jp != j) {
			t = work[jp];
			work[jp] = work[j];
			work[j] = t;
		    }
/* L30: */
		}
	    }
	}

/*        Divide X by 1/SCALE if doing so will not cause overflow. */

	*(unsigned char *)normin = 'Y';
	if (scale != 1.f) {
	    ix = isamax_(n, &work[1], &c__1);
	    if (scale < (r__1 = work[ix], dabs(r__1)) * smlnum || scale == 
		    0.f) {
		goto L40;
	    }
	    srscl_(n, &scale, &work[1], &c__1);
	}
	goto L10;
    }

/*     Compute the estimate of the reciprocal condition number. */

    if (ainvnm != 0.f) {
	*rcond = 1.f / ainvnm / *anorm;
    }

L40:
    return 0;

/*     End of SGBCON */

} /* sgbcon_ */
