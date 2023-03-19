;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2015 Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;
;;; gf_2vect_mad_sse(len, vec, vec_i, mul_array, src, dest);
;;;

%include "reg_sizes.asm"

%define PS 8

%ifidn __OUTPUT_FORMAT__, win64
 %define arg0  rcx
 %define arg0.w ecx
 %define arg1  rdx
 %define arg2  r8
 %define arg3  r9
 %define arg4  r12
 %define arg5  r15
 %define tmp   r11
 %define tmp2   r10
 %define return rax
 %define return.w eax
 %define stack_size 16*9 + 3*8
 %define arg(x)      [rsp + stack_size + PS + PS*x]
 %define func(x) proc_frame x

%macro FUNC_SAVE 0
	sub	rsp, stack_size
	movdqa	[rsp+16*0],xmm6
	movdqa	[rsp+16*1],xmm7
	movdqa	[rsp+16*2],xmm8
	movdqa	[rsp+16*3],xmm9
	movdqa	[rsp+16*4],xmm10
	movdqa	[rsp+16*5],xmm11
	movdqa	[rsp+16*6],xmm12
	movdqa	[rsp+16*7],xmm13
	movdqa	[rsp+16*8],xmm14
	save_reg	r12,  9*16 + 0*8
	save_reg	r15,  9*16 + 1*8
	end_prolog
	mov	arg4, arg(4)
	mov	arg5, arg(5)
%endmacro

%macro FUNC_RESTORE 0
	movdqa	xmm6, [rsp+16*0]
	movdqa	xmm7, [rsp+16*1]
	movdqa	xmm8, [rsp+16*2]
	movdqa	xmm9, [rsp+16*3]
	movdqa	xmm10, [rsp+16*4]
	movdqa	xmm11, [rsp+16*5]
	movdqa	xmm12, [rsp+16*6]
	movdqa	xmm13, [rsp+16*7]
	movdqa	xmm14, [rsp+16*8]
	mov	r12,  [rsp + 9*16 + 0*8]
	mov	r15,  [rsp + 9*16 + 1*8]
	add	rsp, stack_size
%endmacro

%elifidn __OUTPUT_FORMAT__, elf64
 %define arg0  rdi
 %define arg0.w edi
 %define arg1  rsi
 %define arg2  rdx
 %define arg3  rcx
 %define arg4  r8
 %define arg5  r9
 %define tmp   r11
 %define tmp2   r10
 %define return rax
 %define return.w eax

 %define func(x) x:
 %define FUNC_SAVE
 %define FUNC_RESTORE
%endif

;;; gf_2vect_mad_sse(len, vec, vec_i, mul_array, src, dest)
%define len   arg0
%define len.w arg0.w
%define vec    arg1
%define vec_i    arg2
%define mul_array arg3
%define	src   arg4
%define dest1  arg5
%define pos   return
%define pos.w return.w

%define dest2 tmp2

%ifndef EC_ALIGNED_ADDR
;;; Use Un-aligned load/store
 %define XLDR movdqu
 %define XSTR movdqu
%else
;;; Use Non-temporal load/stor
 %ifdef NO_NT_LDST
  %define XLDR movdqa
  %define XSTR movdqa
 %else
  %define XLDR movntdqa
  %define XSTR movntdq
 %endif
%endif

default rel

[bits 64]
section .text

%define xmask0f  xmm14
%define xgft1_lo  xmm13
%define xgft1_hi  xmm12
%define xgft2_lo  xmm11
%define xgft2_hi  xmm10

%define x0      xmm0
%define xtmpa   xmm1
%define xtmph1  xmm2
%define xtmpl1  xmm3
%define xtmph2  xmm4
%define xtmpl2  xmm5
%define xd1     xmm6
%define xd2     xmm7
%define xtmpd1  xmm8
%define xtmpd2  xmm9


align 16
global gf_2vect_mad_sse:ISAL_SYM_TYPE_FUNCTION
func(gf_2vect_mad_sse)
%ifidn __OUTPUT_FORMAT__, macho64
global _gf_2vect_mad_sse:ISAL_SYM_TYPE_FUNCTION
func(_gf_2vect_mad_sse)
%endif

	FUNC_SAVE
	sub	len, 16
	jl	.return_fail

	xor	pos, pos
	movdqa	xmask0f, [mask0f]	;Load mask of lower nibble in each byte
	sal	vec_i, 5		;Multiply by 32
	sal	vec, 5
	lea	tmp, [mul_array + vec_i]
	movdqu	xgft1_lo,[tmp]		;Load array Ax{00}, Ax{01}, Ax{02}, ...
	movdqu	xgft1_hi, [tmp+16]	; " Ax{00}, Ax{10}, Ax{20}, ... , Ax{f0}
	movdqu	xgft2_lo, [tmp+vec]	;Load array Bx{00}, Bx{01}, Bx{02}, ...
	movdqu	xgft2_hi, [tmp+vec+16]	; " Bx{00}, Bx{10}, Bx{20}, ... , Bx{f0}
	mov	dest2, [dest1+PS]
	mov	dest1, [dest1]

	XLDR	xtmpd1, [dest1+len]	;backup the last 16 bytes in dest
	XLDR	xtmpd2, [dest2+len]	;backup the last 16 bytes in dest

.loop16:
	XLDR	xd1, [dest1+pos]		;Get next dest vector
	XLDR	xd2, [dest2+pos]		;Get next dest vector
.loop16_overlap:
	XLDR	x0, [src+pos]		;Get next source vector
	movdqa	xtmph1, xgft1_hi		;Reload const array registers
	movdqa	xtmpl1, xgft1_lo
	movdqa	xtmph2, xgft2_hi		;Reload const array registers
	movdqa	xtmpl2, xgft2_lo
	movdqa	xtmpa, x0		;Keep unshifted copy of src
	psraw	x0, 4			;Shift to put high nibble into bits 4-0
	pand	x0, xmask0f		;Mask high src nibble in bits 4-0
	pand	xtmpa, xmask0f		;Mask low src nibble in bits 4-0

	pshufb	xtmph1, x0		;Lookup mul table of high nibble
	pshufb	xtmpl1, xtmpa		;Lookup mul table of low nibble
	pxor	xtmph1, xtmpl1		;GF add high and low partials
	pxor	xd1, xtmph1

	pshufb	xtmph2, x0		;Lookup mul table of high nibble
	pshufb	xtmpl2, xtmpa		;Lookup mul table of low nibble
	pxor	xtmph2, xtmpl2		;GF add high and low partials
	pxor	xd2, xtmph2

	XSTR	[dest1+pos], xd1	;Store result
	XSTR	[dest2+pos], xd2	;Store result

	add	pos, 16			;Loop on 16 bytes at a time
	cmp	pos, len
	jle	.loop16

	lea	tmp, [len + 16]
	cmp	pos, tmp
	je	.return_pass

	;; Tail len
	mov	pos, len	;Overlapped offset length-16
	movdqa	xd1, xtmpd1	;Restore xd1
	movdqa	xd2, xtmpd2	;Restore xd2
	jmp	.loop16_overlap	;Do one more overlap pass

.return_pass:
	FUNC_RESTORE
	mov	return, 0
	ret

.return_fail:
	FUNC_RESTORE
	mov	return, 1
	ret

endproc_frame

section .data

align 16

mask0f:
	dq 0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f

;;;       func             core, ver, snum
slversion gf_2vect_mad_sse, 00,  01,  0203
