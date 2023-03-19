/* cgeevx.f -- translated by f2c (version 20061008).
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
static integer c__0 = 0;
static integer c_n1 = -1;

/* Subroutine */ int cgeevx_(char *balanc, char *jobvl, char *jobvr, char *
	sense, integer *n, complex *a, integer *lda, complex *w, complex *vl, 
	integer *ldvl, complex *vr, integer *ldvr, integer *ilo, integer *ihi, 
	 real *scale, real *abnrm, real *rconde, real *rcondv, complex *work, 
	integer *lwork, real *rwork, integer *info)
{
    /* System generated locals */
    integer a_dim1, a_offset, vl_dim1, vl_offset, vr_dim1, vr_offset, i__1, 
	    i__2, i__3;
    real r__1, r__2;
    complex q__1, q__2;

    /* Builtin functions */
    double sqrt(doublereal), r_imag(complex *);
    void r_cnjg(complex *, complex *);

    /* Local variables */
    integer i__, k;
    char job[1];
    real scl, dum[1], eps;
    complex tmp;
    char side[1];
    real anrm;
    integer ierr, itau, iwrk, nout;
    extern /* Subroutine */ int cscal_(integer *, complex *, complex *, 
	    integer *);
    integer icond;
    extern logical lsame_(char *, char *);
    extern doublereal scnrm2_(integer *, complex *, integer *);
    extern /* Subroutine */ int cgebak_(char *, char *, integer *, integer *, 
	    integer *, real *, integer *, complex *, integer *, integer *), cgebal_(char *, integer *, complex *, integer *, 
	    integer *, integer *, real *, integer *), slabad_(real *, 
	    real *);
    logical scalea;
    extern doublereal clange_(char *, integer *, integer *, complex *, 
	    integer *, real *);
    real cscale;
    extern /* Subroutine */ int cgehrd_(integer *, integer *, integer *, 
	    complex *, integer *, complex *, complex *, integer *, integer *),
	     clascl_(char *, integer *, integer *, real *, real *, integer *, 
	    integer *, complex *, integer *, integer *);
    extern doublereal slamch_(char *);
    extern /* Subroutine */ int csscal_(integer *, real *, complex *, integer 
	    *), clacpy_(char *, integer *, integer *, complex *, integer *, 
	    complex *, integer *), xerbla_(char *, integer *);
    extern integer ilaenv_(integer *, char *, char *, integer *, integer *, 
	    integer *, integer *);
    logical select[1];
    real bignum;
    extern /* Subroutine */ int slascl_(char *, integer *, integer *, real *, 
	    real *, integer *, integer *, real *, integer *, integer *);
    extern integer isamax_(integer *, real *, integer *);
    extern /* Subroutine */ int chseqr_(char *, char *, integer *, integer *, 
	    integer *, complex *, integer *, complex *, complex *, integer *, 
	    complex *, integer *, integer *), ctrevc_(char *, 
	    char *, logical *, integer *, complex *, integer *, complex *, 
	    integer *, complex *, integer *, integer *, integer *, complex *, 
	    real *, integer *), cunghr_(integer *, integer *, 
	    integer *, complex *, integer *, complex *, complex *, integer *, 
	    integer *), ctrsna_(char *, char *, logical *, integer *, complex 
	    *, integer *, complex *, integer *, complex *, integer *, real *, 
	    real *, integer *, integer *, complex *, integer *, real *, 
	    integer *);
    integer minwrk, maxwrk;
    logical wantvl, wntsnb;
    integer hswork;
    logical wntsne;
    real smlnum;
    logical lquery, wantvr, wntsnn, wntsnv;


/*  -- LAPACK driver routine (version 3.2) -- */
/*     Univ. of Tennessee, Univ. of California Berkeley and NAG Ltd.. */
/*     November 2006 */

/*     .. Scalar Arguments .. */
/*     .. */
/*     .. Array Arguments .. */
/*     .. */

/*  Purpose */
/*  ======= */

/*  CGEEVX computes for an N-by-N complex nonsymmetric matrix A, the */
/*  eigenvalues and, optionally, the left and/or right eigenvectors. */

/*  Optionally also, it computes a balancing transformation to improve */
/*  the conditioning of the eigenvalues and eigenvectors (ILO, IHI, */
/*  SCALE, and ABNRM), reciprocal condition numbers for the eigenvalues */
/*  (RCONDE), and reciprocal condition numbers for the right */
/*  eigenvectors (RCONDV). */

/*  The right eigenvector v(j) of A satisfies */
/*                   A * v(j) = lambda(j) * v(j) */
/*  where lambda(j) is its eigenvalue. */
/*  The left eigenvector u(j) of A satisfies */
/*                u(j)**H * A = lambda(j) * u(j)**H */
/*  where u(j)**H denotes the conjugate transpose of u(j). */

/*  The computed eigenvectors are normalized to have Euclidean norm */
/*  equal to 1 and largest component real. */

/*  Balancing a matrix means permuting the rows and columns to make it */
/*  more nearly upper triangular, and applying a diagonal similarity */
/*  transformation D * A * D**(-1), where D is a diagonal matrix, to */
/*  make its rows and columns closer in norm and the condition numbers */
/*  of its eigenvalues and eigenvectors smaller.  The computed */
/*  reciprocal condition numbers correspond to the balanced matrix. */
/*  Permuting rows and columns will not change the condition numbers */
/*  (in exact arithmetic) but diagonal scaling will.  For further */
/*  explanation of balancing, see section 4.10.2 of the LAPACK */
/*  Users' Guide. */

/*  Arguments */
/*  ========= */

/*  BALANC  (input) CHARACTER*1 */
/*          Indicates how the input matrix should be diagonally scaled */
/*          and/or permuted to improve the conditioning of its */
/*          eigenvalues. */
/*          = 'N': Do not diagonally scale or permute; */
/*          = 'P': Perform permutations to make the matrix more nearly */
/*                 upper triangular. Do not diagonally scale; */
/*          = 'S': Diagonally scale the matrix, ie. replace A by */
/*                 D*A*D**(-1), where D is a diagonal matrix chosen */
/*                 to make the rows and columns of A more equal in */
/*                 norm. Do not permute; */
/*          = 'B': Both diagonally scale and permute A. */

/*          Computed reciprocal condition numbers will be for the matrix */
/*          after balancing and/or permuting. Permuting does not change */
/*          condition numbers (in exact arithmetic), but balancing does. */

/*  JOBVL   (input) CHARACTER*1 */
/*          = 'N': left eigenvectors of A are not computed; */
/*          = 'V': left eigenvectors of A are computed. */
/*          If SENSE = 'E' or 'B', JOBVL must = 'V'. */

/*  JOBVR   (input) CHARACTER*1 */
/*          = 'N': right eigenvectors of A are not computed; */
/*          = 'V': right eigenvectors of A are computed. */
/*          If SENSE = 'E' or 'B', JOBVR must = 'V'. */

/*  SENSE   (input) CHARACTER*1 */
/*          Determines which reciprocal condition numbers are computed. */
/*          = 'N': None are computed; */
/*          = 'E': Computed for eigenvalues only; */
/*          = 'V': Computed for right eigenvectors only; */
/*          = 'B': Computed for eigenvalues and right eigenvectors. */

/*          If SENSE = 'E' or 'B', both left and right eigenvectors */
/*          must also be computed (JOBVL = 'V' and JOBVR = 'V'). */

/*  N       (input) INTEGER */
/*          The order of the matrix A. N >= 0. */

/*  A       (input/output) COMPLEX array, dimension (LDA,N) */
/*          On entry, the N-by-N matrix A. */
/*          On exit, A has been overwritten.  If JOBVL = 'V' or */
/*          JOBVR = 'V', A contains the Schur form of the balanced */
/*          version of the matrix A. */

/*  LDA     (input) INTEGER */
/*          The leading dimension of the array A.  LDA >= max(1,N). */

/*  W       (output) COMPLEX array, dimension (N) */
/*          W contains the computed eigenvalues. */

/*  VL      (output) COMPLEX array, dimension (LDVL,N) */
/*          If JOBVL = 'V', the left eigenvectors u(j) are stored one */
/*          after another in the columns of VL, in the same order */
/*          as their eigenvalues. */
/*          If JOBVL = 'N', VL is not referenced. */
/*          u(j) = VL(:,j), the j-th column of VL. */

/*  LDVL    (input) INTEGER */
/*          The leading dimension of the array VL.  LDVL >= 1; if */
/*          JOBVL = 'V', LDVL >= N. */

/*  VR      (output) COMPLEX array, dimension (LDVR,N) */
/*          If JOBVR = 'V', the right eigenvectors v(j) are stored one */
/*          after another in the columns of VR, in the same order */
/*          as their eigenvalues. */
/*          If JOBVR = 'N', VR is not referenced. */
/*          v(j) = VR(:,j), the j-th column of VR. */

/*  LDVR    (input) INTEGER */
/*          The leading dimension of the array VR.  LDVR >= 1; if */
/*          JOBVR = 'V', LDVR >= N. */

/*  ILO     (output) INTEGER */
/*  IHI     (output) INTEGER */
/*          ILO and IHI are integer values determined when A was */
/*          balanced.  The balanced A(i,j) = 0 if I > J and */
/*          J = 1,...,ILO-1 or I = IHI+1,...,N. */

/*  SCALE   (output) REAL array, dimension (N) */
/*          Details of the permutations and scaling factors applied */
/*          when balancing A.  If P(j) is the index of the row and column */
/*          interchanged with row and column j, and D(j) is the scaling */
/*          factor applied to row and column j, then */
/*          SCALE(J) = P(J),    for J = 1,...,ILO-1 */
/*                   = D(J),    for J = ILO,...,IHI */
/*                   = P(J)     for J = IHI+1,...,N. */
/*          The order in which the interchanges are made is N to IHI+1, */
/*          then 1 to ILO-1. */

/*  ABNRM   (output) REAL */
/*          The one-norm of the balanced matrix (the maximum */
/*          of the sum of absolute values of elements of any column). */

/*  RCONDE  (output) REAL array, dimension (N) */
/*          RCONDE(j) is the reciprocal condition number of the j-th */
/*          eigenvalue. */

/*  RCONDV  (output) REAL array, dimension (N) */
/*          RCONDV(j) is the reciprocal condition number of the j-th */
/*          right eigenvector. */

/*  WORK    (workspace/output) COMPLEX array, dimension (MAX(1,LWORK)) */
/*          On exit, if INFO = 0, WORK(1) returns the optimal LWORK. */

/*  LWORK   (input) INTEGER */
/*          The dimension of the array WORK.  If SENSE = 'N' or 'E', */
/*          LWORK >= max(1,2*N), and if SENSE = 'V' or 'B', */
/*          LWORK >= N*N+2*N. */
/*          For good performance, LWORK must generally be larger. */

/*          If LWORK = -1, then a workspace query is assumed; the routine */
/*          only calculates the optimal size of the WORK array, returns */
/*          this value as the first entry of the WORK array, and no error */
/*          message related to LWORK is issued by XERBLA. */

/*  RWORK   (workspace) REAL array, dimension (2*N) */

/*  INFO    (output) INTEGER */
/*          = 0:  successful exit */
/*          < 0:  if INFO = -i, the i-th argument had an illegal value. */
/*          > 0:  if INFO = i, the QR algorithm failed to compute all the */
/*                eigenvalues, and no eigenvectors or condition numbers */
/*                have been computed; elements 1:ILO-1 and i+1:N of W */
/*                contain eigenvalues which have converged. */

/*  ===================================================================== */

/*     .. Parameters .. */
/*     .. */
/*     .. Local Scalars .. */
/*     .. */
/*     .. Local Arrays .. */
/*     .. */
/*     .. External Subroutines .. */
/*     .. */
/*     .. External Functions .. */
/*     .. */
/*     .. Intrinsic Functions .. */
/*     .. */
/*     .. Executable Statements .. */

/*     Test the input arguments */

    /* Parameter adjustments */
    a_dim1 = *lda;
    a_offset = 1 + a_dim1;
    a -= a_offset;
    --w;
    vl_dim1 = *ldvl;
    vl_offset = 1 + vl_dim1;
    vl -= vl_offset;
    vr_dim1 = *ldvr;
    vr_offset = 1 + vr_dim1;
    vr -= vr_offset;
    --scale;
    --rconde;
    --rcondv;
    --work;
    --rwork;

    /* Function Body */
    *info = 0;
    lquery = *lwork == -1;
    wantvl = lsame_(jobvl, "V");
    wantvr = lsame_(jobvr, "V");
    wntsnn = lsame_(sense, "N");
    wntsne = lsame_(sense, "E");
    wntsnv = lsame_(sense, "V");
    wntsnb = lsame_(sense, "B");
    if (! (lsame_(balanc, "N") || lsame_(balanc, "S") || lsame_(balanc, "P") 
	    || lsame_(balanc, "B"))) {
	*info = -1;
    } else if (! wantvl && ! lsame_(jobvl, "N")) {
	*info = -2;
    } else if (! wantvr && ! lsame_(jobvr, "N")) {
	*info = -3;
    } else if (! (wntsnn || wntsne || wntsnb || wntsnv) || (wntsne || wntsnb) 
	    && ! (wantvl && wantvr)) {
	*info = -4;
    } else if (*n < 0) {
	*info = -5;
    } else if (*lda < max(1,*n)) {
	*info = -7;
    } else if (*ldvl < 1 || wantvl && *ldvl < *n) {
	*info = -10;
    } else if (*ldvr < 1 || wantvr && *ldvr < *n) {
	*info = -12;
    }

/*     Compute workspace */
/*      (Note: Comments in the code beginning "Workspace:" describe the */
/*       minimal amount of workspace needed at that point in the code, */
/*       as well as the preferred amount for good performance. */
/*       CWorkspace refers to complex workspace, and RWorkspace to real */
/*       workspace. NB refers to the optimal block size for the */
/*       immediately following subroutine, as returned by ILAENV. */
/*       HSWORK refers to the workspace preferred by CHSEQR, as */
/*       calculated below. HSWORK is computed assuming ILO=1 and IHI=N, */
/*       the worst case.) */

    if (*info == 0) {
	if (*n == 0) {
	    minwrk = 1;
	    maxwrk = 1;
	} else {
	    maxwrk = *n + *n * ilaenv_(&c__1, "CGEHRD", " ", n, &c__1, n, &
		    c__0);

	    if (wantvl) {
		chseqr_("S", "V", n, &c__1, n, &a[a_offset], lda, &w[1], &vl[
			vl_offset], ldvl, &work[1], &c_n1, info);
	    } else if (wantvr) {
		chseqr_("S", "V", n, &c__1, n, &a[a_offset], lda, &w[1], &vr[
			vr_offset], ldvr, &work[1], &c_n1, info);
	    } else {
		if (wntsnn) {
		    chseqr_("E", "N", n, &c__1, n, &a[a_offset], lda, &w[1], &
			    vr[vr_offset], ldvr, &work[1], &c_n1, info);
		} else {
		    chseqr_("S", "N", n, &c__1, n, &a[a_offset], lda, &w[1], &
			    vr[vr_offset], ldvr, &work[1], &c_n1, info);
		}
	    }
	    hswork = work[1].r;

	    if (! wantvl && ! wantvr) {
		minwrk = *n << 1;
		if (! (wntsnn || wntsne)) {
/* Computing MAX */
		    i__1 = minwrk, i__2 = *n * *n + (*n << 1);
		    minwrk = max(i__1,i__2);
		}
		maxwrk = max(maxwrk,hswork);
		if (! (wntsnn || wntsne)) {
/* Computing MAX */
		    i__1 = maxwrk, i__2 = *n * *n + (*n << 1);
		    maxwrk = max(i__1,i__2);
		}
	    } else {
		minwrk = *n << 1;
		if (! (wntsnn || wntsne)) {
/* Computing MAX */
		    i__1 = minwrk, i__2 = *n * *n + (*n << 1);
		    minwrk = max(i__1,i__2);
		}
		maxwrk = max(maxwrk,hswork);
/* Computing MAX */
		i__1 = maxwrk, i__2 = *n + (*n - 1) * ilaenv_(&c__1, "CUNGHR", 
			 " ", n, &c__1, n, &c_n1);
		maxwrk = max(i__1,i__2);
		if (! (wntsnn || wntsne)) {
/* Computing MAX */
		    i__1 = maxwrk, i__2 = *n * *n + (*n << 1);
		    maxwrk = max(i__1,i__2);
		}
/* Computing MAX */
		i__1 = maxwrk, i__2 = *n << 1;
		maxwrk = max(i__1,i__2);
	    }
	    maxwrk = max(maxwrk,minwrk);
	}
	work[1].r = (real) maxwrk, work[1].i = 0.f;

	if (*lwork < minwrk && ! lquery) {
	    *info = -20;
	}
    }

    if (*info != 0) {
	i__1 = -(*info);
	xerbla_("CGEEVX", &i__1);
	return 0;
    } else if (lquery) {
	return 0;
    }

/*     Quick return if possible */

    if (*n == 0) {
	return 0;
    }

/*     Get machine constants */

    eps = slamch_("P");
    smlnum = slamch_("S");
    bignum = 1.f / smlnum;
    slabad_(&smlnum, &bignum);
    smlnum = sqrt(smlnum) / eps;
    bignum = 1.f / smlnum;

/*     Scale A if max element outside range [SMLNUM,BIGNUM] */

    icond = 0;
    anrm = clange_("M", n, n, &a[a_offset], lda, dum);
    scalea = FALSE_;
    if (anrm > 0.f && anrm < smlnum) {
	scalea = TRUE_;
	cscale = smlnum;
    } else if (anrm > bignum) {
	scalea = TRUE_;
	cscale = bignum;
    }
    if (scalea) {
	clascl_("G", &c__0, &c__0, &anrm, &cscale, n, n, &a[a_offset], lda, &
		ierr);
    }

/*     Balance the matrix and compute ABNRM */

    cgebal_(balanc, n, &a[a_offset], lda, ilo, ihi, &scale[1], &ierr);
    *abnrm = clange_("1", n, n, &a[a_offset], lda, dum);
    if (scalea) {
	dum[0] = *abnrm;
	slascl_("G", &c__0, &c__0, &cscale, &anrm, &c__1, &c__1, dum, &c__1, &
		ierr);
	*abnrm = dum[0];
    }

/*     Reduce to upper Hessenberg form */
/*     (CWorkspace: need 2*N, prefer N+N*NB) */
/*     (RWorkspace: none) */

    itau = 1;
    iwrk = itau + *n;
    i__1 = *lwork - iwrk + 1;
    cgehrd_(n, ilo, ihi, &a[a_offset], lda, &work[itau], &work[iwrk], &i__1, &
	    ierr);

    if (wantvl) {

/*        Want left eigenvectors */
/*        Copy Householder vectors to VL */

	*(unsigned char *)side = 'L';
	clacpy_("L", n, n, &a[a_offset], lda, &vl[vl_offset], ldvl)
		;

/*        Generate unitary matrix in VL */
/*        (CWorkspace: need 2*N-1, prefer N+(N-1)*NB) */
/*        (RWorkspace: none) */

	i__1 = *lwork - iwrk + 1;
	cunghr_(n, ilo, ihi, &vl[vl_offset], ldvl, &work[itau], &work[iwrk], &
		i__1, &ierr);

/*        Perform QR iteration, accumulating Schur vectors in VL */
/*        (CWorkspace: need 1, prefer HSWORK (see comments) ) */
/*        (RWorkspace: none) */

	iwrk = itau;
	i__1 = *lwork - iwrk + 1;
	chseqr_("S", "V", n, ilo, ihi, &a[a_offset], lda, &w[1], &vl[
		vl_offset], ldvl, &work[iwrk], &i__1, info);

	if (wantvr) {

/*           Want left and right eigenvectors */
/*           Copy Schur vectors to VR */

	    *(unsigned char *)side = 'B';
	    clacpy_("F", n, n, &vl[vl_offset], ldvl, &vr[vr_offset], ldvr);
	}

    } else if (wantvr) {

/*        Want right eigenvectors */
/*        Copy Householder vectors to VR */

	*(unsigned char *)side = 'R';
	clacpy_("L", n, n, &a[a_offset], lda, &vr[vr_offset], ldvr)
		;

/*        Generate unitary matrix in VR */
/*        (CWorkspace: need 2*N-1, prefer N+(N-1)*NB) */
/*        (RWorkspace: none) */

	i__1 = *lwork - iwrk + 1;
	cunghr_(n, ilo, ihi, &vr[vr_offset], ldvr, &work[itau], &work[iwrk], &
		i__1, &ierr);

/*        Perform QR iteration, accumulating Schur vectors in VR */
/*        (CWorkspace: need 1, prefer HSWORK (see comments) ) */
/*        (RWorkspace: none) */

	iwrk = itau;
	i__1 = *lwork - iwrk + 1;
	chseqr_("S", "V", n, ilo, ihi, &a[a_offset], lda, &w[1], &vr[
		vr_offset], ldvr, &work[iwrk], &i__1, info);

    } else {

/*        Compute eigenvalues only */
/*        If condition numbers desired, compute Schur form */

	if (wntsnn) {
	    *(unsigned char *)job = 'E';
	} else {
	    *(unsigned char *)job = 'S';
	}

/*        (CWorkspace: need 1, prefer HSWORK (see comments) ) */
/*        (RWorkspace: none) */

	iwrk = itau;
	i__1 = *lwork - iwrk + 1;
	chseqr_(job, "N", n, ilo, ihi, &a[a_offset], lda, &w[1], &vr[
		vr_offset], ldvr, &work[iwrk], &i__1, info);
    }

/*     If INFO > 0 from CHSEQR, then quit */

    if (*info > 0) {
	goto L50;
    }

    if (wantvl || wantvr) {

/*        Compute left and/or right eigenvectors */
/*        (CWorkspace: need 2*N) */
/*        (RWorkspace: need N) */

	ctrevc_(side, "B", select, n, &a[a_offset], lda, &vl[vl_offset], ldvl, 
		 &vr[vr_offset], ldvr, n, &nout, &work[iwrk], &rwork[1], &
		ierr);
    }

/*     Compute condition numbers if desired */
/*     (CWorkspace: need N*N+2*N unless SENSE = 'E') */
/*     (RWorkspace: need 2*N unless SENSE = 'E') */

    if (! wntsnn) {
	ctrsna_(sense, "A", select, n, &a[a_offset], lda, &vl[vl_offset], 
		ldvl, &vr[vr_offset], ldvr, &rconde[1], &rcondv[1], n, &nout, 
		&work[iwrk], n, &rwork[1], &icond);
    }

    if (wantvl) {

/*        Undo balancing of left eigenvectors */

	cgebak_(balanc, "L", n, ilo, ihi, &scale[1], n, &vl[vl_offset], ldvl, 
		&ierr);

/*        Normalize left eigenvectors and make largest component real */

	i__1 = *n;
	for (i__ = 1; i__ <= i__1; ++i__) {
	    scl = 1.f / scnrm2_(n, &vl[i__ * vl_dim1 + 1], &c__1);
	    csscal_(n, &scl, &vl[i__ * vl_dim1 + 1], &c__1);
	    i__2 = *n;
	    for (k = 1; k <= i__2; ++k) {
		i__3 = k + i__ * vl_dim1;
/* Computing 2nd power */
		r__1 = vl[i__3].r;
/* Computing 2nd power */
		r__2 = r_imag(&vl[k + i__ * vl_dim1]);
		rwork[k] = r__1 * r__1 + r__2 * r__2;
/* L10: */
	    }
	    k = isamax_(n, &rwork[1], &c__1);
	    r_cnjg(&q__2, &vl[k + i__ * vl_dim1]);
	    r__1 = sqrt(rwork[k]);
	    q__1.r = q__2.r / r__1, q__1.i = q__2.i / r__1;
	    tmp.r = q__1.r, tmp.i = q__1.i;
	    cscal_(n, &tmp, &vl[i__ * vl_dim1 + 1], &c__1);
	    i__2 = k + i__ * vl_dim1;
	    i__3 = k + i__ * vl_dim1;
	    r__1 = vl[i__3].r;
	    q__1.r = r__1, q__1.i = 0.f;
	    vl[i__2].r = q__1.r, vl[i__2].i = q__1.i;
/* L20: */
	}
    }

    if (wantvr) {

/*        Undo balancing of right eigenvectors */

	cgebak_(balanc, "R", n, ilo, ihi, &scale[1], n, &vr[vr_offset], ldvr, 
		&ierr);

/*        Normalize right eigenvectors and make largest component real */

	i__1 = *n;
	for (i__ = 1; i__ <= i__1; ++i__) {
	    scl = 1.f / scnrm2_(n, &vr[i__ * vr_dim1 + 1], &c__1);
	    csscal_(n, &scl, &vr[i__ * vr_dim1 + 1], &c__1);
	    i__2 = *n;
	    for (k = 1; k <= i__2; ++k) {
		i__3 = k + i__ * vr_dim1;
/* Computing 2nd power */
		r__1 = vr[i__3].r;
/* Computing 2nd power */
		r__2 = r_imag(&vr[k + i__ * vr_dim1]);
		rwork[k] = r__1 * r__1 + r__2 * r__2;
/* L30: */
	    }
	    k = isamax_(n, &rwork[1], &c__1);
	    r_cnjg(&q__2, &vr[k + i__ * vr_dim1]);
	    r__1 = sqrt(rwork[k]);
	    q__1.r = q__2.r / r__1, q__1.i = q__2.i / r__1;
	    tmp.r = q__1.r, tmp.i = q__1.i;
	    cscal_(n, &tmp, &vr[i__ * vr_dim1 + 1], &c__1);
	    i__2 = k + i__ * vr_dim1;
	    i__3 = k + i__ * vr_dim1;
	    r__1 = vr[i__3].r;
	    q__1.r = r__1, q__1.i = 0.f;
	    vr[i__2].r = q__1.r, vr[i__2].i = q__1.i;
/* L40: */
	}
    }

/*     Undo scaling if necessary */

L50:
    if (scalea) {
	i__1 = *n - *info;
/* Computing MAX */
	i__3 = *n - *info;
	i__2 = max(i__3,1);
	clascl_("G", &c__0, &c__0, &cscale, &anrm, &i__1, &c__1, &w[*info + 1]
, &i__2, &ierr);
	if (*info == 0) {
	    if ((wntsnv || wntsnb) && icond == 0) {
		slascl_("G", &c__0, &c__0, &cscale, &anrm, n, &c__1, &rcondv[
			1], n, &ierr);
	    }
	} else {
	    i__1 = *ilo - 1;
	    clascl_("G", &c__0, &c__0, &cscale, &anrm, &i__1, &c__1, &w[1], n, 
		     &ierr);
	}
    }

    work[1].r = (real) maxwrk, work[1].i = 0.f;
    return 0;

/*     End of CGEEVX */

} /* cgeevx_ */
