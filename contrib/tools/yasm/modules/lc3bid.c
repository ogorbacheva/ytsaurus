/* Generated by re2c
 */
/*
 * LC-3b identifier recognition and instruction handling
 *
 *  Copyright (C) 2003-2007  Peter Johnson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND OTHER CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR OTHER CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <util.h>

#include <libyasm.h>

#include "modules/arch/lc3b/lc3barch.h"


/* Opcode modifiers.  The opcode bytes are in "reverse" order because the
 * parameters are read from the arch-specific data in LSB->MSB order.
 * (only for asthetic reasons in the lexer code below, no practical reason).
 */
#define MOD_OpHAdd  (1UL<<0)    /* Parameter adds to upper 8 bits of insn */
#define MOD_OpLAdd  (1UL<<1)    /* Parameter adds to lower 8 bits of insn */

/* Operand types.  These are more detailed than the "general" types for all
 * architectures, as they include the size, for instance.
 * Bit Breakdown (from LSB to MSB):
 *  - 1 bit = general type (must be exact match, except for =3):
 *            0 = immediate
 *            1 = register
 *
 * MSBs than the above are actions: what to do with the operand if the
 * instruction matches.  Essentially describes what part of the output bytecode
 * gets the operand.  This may require conversion (e.g. a register going into
 * an ea field).  Naturally, only one of each of these may be contained in the
 * operands of a single insn_info structure.
 *  - 2 bits = action:
 *             0 = does nothing (operand data is discarded)
 *             1 = DR field
 *             2 = SR field
 *             3 = immediate
 *
 * Immediate operands can have different sizes.
 *  - 3 bits = size:
 *             0 = no immediate
 *             1 = 4-bit immediate
 *             2 = 5-bit immediate
 *             3 = 6-bit index, word (16 bit)-multiple
 *             4 = 6-bit index, byte-multiple
 *             5 = 8-bit immediate, word-multiple
 *             6 = 9-bit signed immediate, word-multiple
 *             7 = 9-bit signed offset from next PC ($+2), word-multiple
 */
#define OPT_Imm         0x0
#define OPT_Reg         0x1
#define OPT_MASK        0x1

#define OPA_None        (0<<1)
#define OPA_DR          (1<<1)
#define OPA_SR          (2<<1)
#define OPA_Imm         (3<<1)
#define OPA_MASK        (3<<1)

#define OPI_None        (LC3B_IMM_NONE<<3)
#define OPI_4           (LC3B_IMM_4<<3)
#define OPI_5           (LC3B_IMM_5<<3)
#define OPI_6W          (LC3B_IMM_6_WORD<<3)
#define OPI_6B          (LC3B_IMM_6_BYTE<<3)
#define OPI_8           (LC3B_IMM_8<<3)
#define OPI_9           (LC3B_IMM_9<<3)
#define OPI_9PC         (LC3B_IMM_9_PC<<3)
#define OPI_MASK        (7<<3)

typedef struct lc3b_insn_info {
    /* Opcode modifiers for variations of instruction.  As each modifier reads
     * its parameter in LSB->MSB order from the arch-specific data[1] from the
     * lexer data, and the LSB of the arch-specific data[1] is reserved for the
     * count of insn_info structures in the instruction grouping, there can
     * only be a maximum of 3 modifiers.
     */
    unsigned int modifiers;

    /* The basic 2 byte opcode */
    unsigned int opcode;

    /* The number of operands this form of the instruction takes */
    unsigned char num_operands;

    /* The types of each operand, see above */
    unsigned int operands[3];
} lc3b_insn_info;

typedef struct lc3b_id_insn {
    yasm_insn insn;     /* base structure */

    /* instruction parse group - NULL if empty instruction (just prefixes) */
    /*@null@*/ const lc3b_insn_info *group;

    /* Modifier data */
    unsigned long mod_data;

    /* Number of elements in the instruction parse group */
    unsigned int num_info:8;
} lc3b_id_insn;

static void lc3b_id_insn_destroy(void *contents);
static void lc3b_id_insn_print(const void *contents, FILE *f, int indent_level);
static void lc3b_id_insn_finalize(yasm_bytecode *bc, yasm_bytecode *prev_bc);

static const yasm_bytecode_callback lc3b_id_insn_callback = {
    lc3b_id_insn_destroy,
    lc3b_id_insn_print,
    lc3b_id_insn_finalize,
    NULL,
    yasm_bc_calc_len_common,
    yasm_bc_expand_common,
    yasm_bc_tobytes_common,
    YASM_BC_SPECIAL_INSN
};

/*
 * Instruction groupings
 */

static const lc3b_insn_info empty_insn[] = {
    { 0, 0, 0, {0, 0, 0} }
};

static const lc3b_insn_info addand_insn[] = {
    { MOD_OpHAdd, 0x1000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Reg|OPA_Imm|OPI_5} },
    { MOD_OpHAdd, 0x1020, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_5} }
};

static const lc3b_insn_info br_insn[] = {
    { MOD_OpHAdd, 0x0000, 1, {OPT_Imm|OPA_Imm|OPI_9PC, 0, 0} }
};

static const lc3b_insn_info jmp_insn[] = {
    { 0, 0xC000, 2, {OPT_Reg|OPA_DR, OPT_Imm|OPA_Imm|OPI_9, 0} }
};

static const lc3b_insn_info lea_insn[] = {
    { 0, 0xE000, 2, {OPT_Reg|OPA_DR, OPT_Imm|OPA_Imm|OPI_9PC, 0} }
};

static const lc3b_insn_info ldst_insn[] = {
    { MOD_OpHAdd, 0x0000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_6W} }
};

static const lc3b_insn_info ldstb_insn[] = {
    { MOD_OpHAdd, 0x0000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_6B} }
};

static const lc3b_insn_info not_insn[] = {
    { 0, 0x903F, 2, {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, 0} }
};

static const lc3b_insn_info nooperand_insn[] = {
    { MOD_OpHAdd, 0x0000, 0, {0, 0, 0} }
};

static const lc3b_insn_info shift_insn[] = {
    { MOD_OpLAdd, 0xD000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_4} }
};

static const lc3b_insn_info trap_insn[] = {
    { 0, 0xF000, 1, {OPT_Imm|OPA_Imm|OPI_8, 0, 0} }
};

static void
lc3b_id_insn_finalize(yasm_bytecode *bc, yasm_bytecode *prev_bc)
{
    lc3b_id_insn *id_insn = (lc3b_id_insn *)bc->contents;
    lc3b_insn *insn;
    int num_info = id_insn->num_info;
    const lc3b_insn_info *info = id_insn->group;
    unsigned long mod_data = id_insn->mod_data;
    int found = 0;
    yasm_insn_operand *op;
    int i;

    yasm_insn_finalize(&id_insn->insn);

    /* Just do a simple linear search through the info array for a match.
     * First match wins.
     */
    for (; num_info>0 && !found; num_info--, info++) {
        int mismatch = 0;

        /* Match # of operands */
        if (id_insn->insn.num_operands != info->num_operands)
            continue;

        if (id_insn->insn.num_operands == 0) {
            found = 1;      /* no operands -> must have a match here. */
            break;
        }

        /* Match each operand type and size */
        for(i = 0, op = yasm_insn_ops_first(&id_insn->insn);
            op && i<info->num_operands && !mismatch;
            op = yasm_insn_op_next(op), i++) {
            /* Check operand type */
            switch ((int)(info->operands[i] & OPT_MASK)) {
                case OPT_Imm:
                    if (op->type != YASM_INSN__OPERAND_IMM)
                        mismatch = 1;
                    break;
                case OPT_Reg:
                    if (op->type != YASM_INSN__OPERAND_REG)
                        mismatch = 1;
                    break;
                default:
                    yasm_internal_error(N_("invalid operand type"));
            }

            if (mismatch)
                break;
        }

        if (!mismatch) {
            found = 1;
            break;
        }
    }

    if (!found) {
        /* Didn't find a matching one */
        yasm_error_set(YASM_ERROR_TYPE,
                       N_("invalid combination of opcode and operands"));
        return;
    }

    /* Copy what we can from info */
    insn = yasm_xmalloc(sizeof(lc3b_insn));
    yasm_value_initialize(&insn->imm, NULL, 0);
    insn->imm_type = LC3B_IMM_NONE;
    insn->opcode = info->opcode;

    /* Apply modifiers */
    if (info->modifiers & MOD_OpHAdd) {
        insn->opcode += ((unsigned int)(mod_data & 0xFF))<<8;
        mod_data >>= 8;
    }
    if (info->modifiers & MOD_OpLAdd) {
        insn->opcode += (unsigned int)(mod_data & 0xFF);
        /*mod_data >>= 8;*/
    }

    /* Go through operands and assign */
    if (id_insn->insn.num_operands > 0) {
        for(i = 0, op = yasm_insn_ops_first(&id_insn->insn);
            op && i<info->num_operands; op = yasm_insn_op_next(op), i++) {

            switch ((int)(info->operands[i] & OPA_MASK)) {
                case OPA_None:
                    /* Throw away the operand contents */
                    if (op->type == YASM_INSN__OPERAND_IMM)
                        yasm_expr_destroy(op->data.val);
                    break;
                case OPA_DR:
                    if (op->type != YASM_INSN__OPERAND_REG)
                        yasm_internal_error(N_("invalid operand conversion"));
                    insn->opcode |= ((unsigned int)(op->data.reg & 0x7)) << 9;
                    break;
                case OPA_SR:
                    if (op->type != YASM_INSN__OPERAND_REG)
                        yasm_internal_error(N_("invalid operand conversion"));
                    insn->opcode |= ((unsigned int)(op->data.reg & 0x7)) << 6;
                    break;
                case OPA_Imm:
                    insn->imm_type = (info->operands[i] & OPI_MASK)>>3;
                    switch (op->type) {
                        case YASM_INSN__OPERAND_IMM:
                            if (insn->imm_type == LC3B_IMM_6_WORD
                                || insn->imm_type == LC3B_IMM_8
                                || insn->imm_type == LC3B_IMM_9
                                || insn->imm_type == LC3B_IMM_9_PC)
                                op->data.val = yasm_expr_create(YASM_EXPR_SHR,
                                    yasm_expr_expr(op->data.val),
                                    yasm_expr_int(yasm_intnum_create_uint(1)),
                                    op->data.val->line);
                            if (yasm_value_finalize_expr(&insn->imm,
                                                         op->data.val,
                                                         prev_bc, 0))
                                yasm_error_set(YASM_ERROR_TOO_COMPLEX,
                                    N_("immediate expression too complex"));
                            break;
                        case YASM_INSN__OPERAND_REG:
                            if (yasm_value_finalize_expr(&insn->imm,
                                    yasm_expr_create_ident(yasm_expr_int(
                                    yasm_intnum_create_uint(op->data.reg & 0x7)),
                                    bc->line), prev_bc, 0))
                                yasm_internal_error(N_("reg expr too complex?"));
                            break;
                        default:
                            yasm_internal_error(N_("invalid operand conversion"));
                    }
                    break;
                default:
                    yasm_internal_error(N_("unknown operand action"));
            }

            /* Clear so it doesn't get destroyed */
            op->type = YASM_INSN__OPERAND_REG;
        }

        if (insn->imm_type == LC3B_IMM_9_PC) {
            if (insn->imm.seg_of || insn->imm.rshift > 1
                || insn->imm.curpos_rel)
                yasm_error_set(YASM_ERROR_VALUE, N_("invalid jump target"));
            insn->imm.curpos_rel = 1;
        }
    }

    /* Transform the bytecode */
    yasm_lc3b__bc_transform_insn(bc, insn);
}


#define YYCTYPE         unsigned char
#define YYCURSOR        id
#define YYLIMIT         id
#define YYMARKER        marker
#define YYFILL(n)       (void)(n)

yasm_arch_regtmod
yasm_lc3b__parse_check_regtmod(yasm_arch *arch, const char *oid, size_t id_len,
                               uintptr_t *data)
{
    const YYCTYPE *id = (const YYCTYPE *)oid;
    /*const char *marker;*/
    
{
	YYCTYPE yych;
	goto yy0;
	++YYCURSOR;
yy0:
	if((YYLIMIT - YYCURSOR) < 3) YYFILL(3);
	yych = *YYCURSOR;
	if(yych <= 'R'){
		if(yych <= '\000')	goto yy6;
		if(yych <= 'Q')	goto yy4;
		goto yy2;
	} else {
		if(yych != 'r')	goto yy4;
		goto yy2;
	}
yy2:	yych = *++YYCURSOR;
	if(yych <= '/')	goto yy5;
	if(yych <= '7')	goto yy8;
	goto yy5;
yy3:
{
            return YASM_ARCH_NOTREGTMOD;
        }
yy4:	++YYCURSOR;
	if(YYLIMIT == YYCURSOR) YYFILL(1);
	yych = *YYCURSOR;
	goto yy5;
yy5:	if(yych <= '\000')	goto yy3;
	goto yy4;
yy6:	yych = *++YYCURSOR;
	goto yy7;
yy7:
{
            return YASM_ARCH_NOTREGTMOD;
        }
yy8:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy4;
	goto yy9;
yy9:
{
            *data = (oid[1]-'0');
            return YASM_ARCH_REG;
        }
}

}

#define RET_INSN(g, m) \
    do { \
        group = g##_insn; \
        mod = m; \
        nelems = NELEMS(g##_insn); \
        goto done; \
    } while(0)

yasm_arch_insnprefix
yasm_lc3b__parse_check_insnprefix(yasm_arch *arch, const char *oid,
                                  size_t id_len, unsigned long line,
                                  yasm_bytecode **bc, uintptr_t *prefix)
{
    const YYCTYPE *id = (const YYCTYPE *)oid;
    const lc3b_insn_info *group = empty_insn;
    unsigned long mod = 0;
    unsigned int nelems = NELEMS(empty_insn);
    lc3b_id_insn *id_insn;

    *bc = (yasm_bytecode *)NULL;
    *prefix = 0;

    /*const char *marker;*/
    
{
	YYCTYPE yych;
	goto yy10;
	++YYCURSOR;
yy10:
	if((YYLIMIT - YYCURSOR) < 6) YYFILL(6);
	yych = *YYCURSOR;
	if(yych <= 'T'){
		if(yych <= 'K'){
			if(yych <= 'A'){
				if(yych <= '\000')	goto yy23;
				if(yych <= '@')	goto yy21;
				goto yy12;
			} else {
				if(yych <= 'B')	goto yy14;
				if(yych == 'J')	goto yy15;
				goto yy21;
			}
		} else {
			if(yych <= 'N'){
				if(yych <= 'L')	goto yy16;
				if(yych <= 'M')	goto yy21;
				goto yy18;
			} else {
				if(yych <= 'Q')	goto yy21;
				if(yych <= 'R')	goto yy19;
				if(yych <= 'S')	goto yy17;
				goto yy20;
			}
		}
	} else {
		if(yych <= 'l'){
			if(yych <= 'b'){
				if(yych <= '`')	goto yy21;
				if(yych >= 'b')	goto yy14;
				goto yy12;
			} else {
				if(yych == 'j')	goto yy15;
				if(yych <= 'k')	goto yy21;
				goto yy16;
			}
		} else {
			if(yych <= 'q'){
				if(yych == 'n')	goto yy18;
				goto yy21;
			} else {
				if(yych <= 'r')	goto yy19;
				if(yych <= 's')	goto yy17;
				if(yych <= 't')	goto yy20;
				goto yy21;
			}
		}
	}
yy12:	yych = *++YYCURSOR;
	if(yych <= 'N'){
		if(yych == 'D')	goto yy88;
		if(yych <= 'M')	goto yy22;
		goto yy89;
	} else {
		if(yych <= 'd'){
			if(yych <= 'c')	goto yy22;
			goto yy88;
		} else {
			if(yych == 'n')	goto yy89;
			goto yy22;
		}
	}
yy13:
{
            return YASM_ARCH_NOTINSNPREFIX;
        }
yy14:	yych = *++YYCURSOR;
	if(yych == 'R')	goto yy72;
	if(yych == 'r')	goto yy72;
	goto yy22;
yy15:	yych = *++YYCURSOR;
	if(yych <= 'S'){
		if(yych == 'M')	goto yy66;
		if(yych <= 'R')	goto yy22;
		goto yy67;
	} else {
		if(yych <= 'm'){
			if(yych <= 'l')	goto yy22;
			goto yy66;
		} else {
			if(yych == 's')	goto yy67;
			goto yy22;
		}
	}
yy16:	yych = *++YYCURSOR;
	if(yych <= 'S'){
		if(yych <= 'D'){
			if(yych <= 'C')	goto yy22;
			goto yy53;
		} else {
			if(yych <= 'E')	goto yy55;
			if(yych <= 'R')	goto yy22;
			goto yy56;
		}
	} else {
		if(yych <= 'e'){
			if(yych <= 'c')	goto yy22;
			if(yych <= 'd')	goto yy53;
			goto yy55;
		} else {
			if(yych == 's')	goto yy56;
			goto yy22;
		}
	}
yy17:	yych = *++YYCURSOR;
	if(yych == 'T')	goto yy47;
	if(yych == 't')	goto yy47;
	goto yy22;
yy18:	yych = *++YYCURSOR;
	if(yych == 'O')	goto yy42;
	if(yych == 'o')	goto yy42;
	goto yy22;
yy19:	yych = *++YYCURSOR;
	if(yych <= 'T'){
		if(yych <= 'E'){
			if(yych <= 'D')	goto yy22;
			goto yy29;
		} else {
			if(yych <= 'R')	goto yy22;
			if(yych <= 'S')	goto yy30;
			goto yy31;
		}
	} else {
		if(yych <= 'r'){
			if(yych == 'e')	goto yy29;
			goto yy22;
		} else {
			if(yych <= 's')	goto yy30;
			if(yych <= 't')	goto yy31;
			goto yy22;
		}
	}
yy20:	yych = *++YYCURSOR;
	if(yych == 'R')	goto yy25;
	if(yych == 'r')	goto yy25;
	goto yy22;
yy21:	++YYCURSOR;
	if(YYLIMIT == YYCURSOR) YYFILL(1);
	yych = *YYCURSOR;
	goto yy22;
yy22:	if(yych <= '\000')	goto yy13;
	goto yy21;
yy23:	yych = *++YYCURSOR;
	goto yy24;
yy24:
{
            return YASM_ARCH_NOTINSNPREFIX;
        }
yy25:	yych = *++YYCURSOR;
	if(yych == 'A')	goto yy26;
	if(yych != 'a')	goto yy22;
	goto yy26;
yy26:	yych = *++YYCURSOR;
	if(yych == 'P')	goto yy27;
	if(yych != 'p')	goto yy22;
	goto yy27;
yy27:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy28;
yy28:
{ RET_INSN(trap, 0); }
yy29:	yych = *++YYCURSOR;
	if(yych == 'T')	goto yy40;
	if(yych == 't')	goto yy40;
	goto yy22;
yy30:	yych = *++YYCURSOR;
	if(yych == 'H')	goto yy34;
	if(yych == 'h')	goto yy34;
	goto yy22;
yy31:	yych = *++YYCURSOR;
	if(yych == 'I')	goto yy32;
	if(yych != 'i')	goto yy22;
	goto yy32;
yy32:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy33;
yy33:
{ RET_INSN(nooperand, 0x80); }
yy34:	yych = *++YYCURSOR;
	if(yych == 'F')	goto yy35;
	if(yych != 'f')	goto yy22;
	goto yy35;
yy35:	yych = *++YYCURSOR;
	if(yych <= 'L'){
		if(yych == 'A')	goto yy38;
		if(yych <= 'K')	goto yy22;
		goto yy36;
	} else {
		if(yych <= 'a'){
			if(yych <= '`')	goto yy22;
			goto yy38;
		} else {
			if(yych != 'l')	goto yy22;
			goto yy36;
		}
	}
yy36:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy37;
yy37:
{ RET_INSN(shift, 0x10); }
yy38:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy39;
yy39:
{ RET_INSN(shift, 0x30); }
yy40:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy41;
yy41:
{ RET_INSN(nooperand, 0xCE); }
yy42:	yych = *++YYCURSOR;
	if(yych <= 'T'){
		if(yych == 'P')	goto yy45;
		if(yych <= 'S')	goto yy22;
		goto yy43;
	} else {
		if(yych <= 'p'){
			if(yych <= 'o')	goto yy22;
			goto yy45;
		} else {
			if(yych != 't')	goto yy22;
			goto yy43;
		}
	}
yy43:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy44;
yy44:
{ RET_INSN(not, 0); }
yy45:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy46;
yy46:
{ RET_INSN(nooperand, 0); }
yy47:	yych = *++YYCURSOR;
	if(yych <= 'I'){
		if(yych <= 'A'){
			if(yych >= '\001')	goto yy21;
			goto yy48;
		} else {
			if(yych <= 'B')	goto yy51;
			if(yych <= 'H')	goto yy21;
			goto yy49;
		}
	} else {
		if(yych <= 'b'){
			if(yych <= 'a')	goto yy21;
			goto yy51;
		} else {
			if(yych == 'i')	goto yy49;
			goto yy21;
		}
	}
yy48:
{ RET_INSN(ldst, 0x30); }
yy49:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy50;
yy50:
{ RET_INSN(ldst, 0xB0); }
yy51:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy52;
yy52:
{ RET_INSN(ldstb, 0x70); }
yy53:	yych = *++YYCURSOR;
	if(yych <= 'I'){
		if(yych <= 'A'){
			if(yych >= '\001')	goto yy21;
			goto yy54;
		} else {
			if(yych <= 'B')	goto yy64;
			if(yych <= 'H')	goto yy21;
			goto yy62;
		}
	} else {
		if(yych <= 'b'){
			if(yych <= 'a')	goto yy21;
			goto yy64;
		} else {
			if(yych == 'i')	goto yy62;
			goto yy21;
		}
	}
yy54:
{ RET_INSN(ldst, 0x20); }
yy55:	yych = *++YYCURSOR;
	if(yych == 'A')	goto yy60;
	if(yych == 'a')	goto yy60;
	goto yy22;
yy56:	yych = *++YYCURSOR;
	if(yych == 'H')	goto yy57;
	if(yych != 'h')	goto yy22;
	goto yy57;
yy57:	yych = *++YYCURSOR;
	if(yych == 'F')	goto yy58;
	if(yych != 'f')	goto yy22;
	goto yy58;
yy58:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy59;
yy59:
{ RET_INSN(shift, 0x00); }
yy60:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy61;
yy61:
{ RET_INSN(lea, 0); }
yy62:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy63;
yy63:
{ RET_INSN(ldst, 0xA0); }
yy64:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy65;
yy65:
{ RET_INSN(ldstb, 0x60); }
yy66:	yych = *++YYCURSOR;
	if(yych == 'P')	goto yy70;
	if(yych == 'p')	goto yy70;
	goto yy22;
yy67:	yych = *++YYCURSOR;
	if(yych == 'R')	goto yy68;
	if(yych != 'r')	goto yy22;
	goto yy68;
yy68:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy69;
yy69:
{ RET_INSN(br, 0x40); }
yy70:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy71;
yy71:
{ RET_INSN(jmp, 0); }
yy72:	yych = *++YYCURSOR;
	if(yych <= 'Z'){
		if(yych <= 'N'){
			if(yych <= '\000')	goto yy73;
			if(yych <= 'M')	goto yy21;
			goto yy74;
		} else {
			if(yych == 'P')	goto yy78;
			if(yych <= 'Y')	goto yy21;
			goto yy76;
		}
	} else {
		if(yych <= 'o'){
			if(yych == 'n')	goto yy74;
			goto yy21;
		} else {
			if(yych <= 'p')	goto yy78;
			if(yych == 'z')	goto yy76;
			goto yy21;
		}
	}
yy73:
{ RET_INSN(br, 0x00); }
yy74:	yych = *++YYCURSOR;
	if(yych <= 'Z'){
		if(yych <= 'O'){
			if(yych >= '\001')	goto yy21;
			goto yy75;
		} else {
			if(yych <= 'P')	goto yy82;
			if(yych <= 'Y')	goto yy21;
			goto yy84;
		}
	} else {
		if(yych <= 'p'){
			if(yych <= 'o')	goto yy21;
			goto yy82;
		} else {
			if(yych == 'z')	goto yy84;
			goto yy21;
		}
	}
yy75:
{ RET_INSN(br, 0x08); }
yy76:	yych = *++YYCURSOR;
	if(yych <= 'P'){
		if(yych <= '\000')	goto yy77;
		if(yych <= 'O')	goto yy21;
		goto yy80;
	} else {
		if(yych == 'p')	goto yy80;
		goto yy21;
	}
yy77:
{ RET_INSN(br, 0x04); }
yy78:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy79;
yy79:
{ RET_INSN(br, 0x02); }
yy80:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy81;
yy81:
{ RET_INSN(br, 0x06); }
yy82:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy83;
yy83:
{ RET_INSN(br, 0x0A); }
yy84:	yych = *++YYCURSOR;
	if(yych <= 'P'){
		if(yych <= '\000')	goto yy85;
		if(yych <= 'O')	goto yy21;
		goto yy86;
	} else {
		if(yych == 'p')	goto yy86;
		goto yy21;
	}
yy85:
{ RET_INSN(br, 0x0C); }
yy86:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy87;
yy87:
{ RET_INSN(br, 0x0E); }
yy88:	yych = *++YYCURSOR;
	if(yych == 'D')	goto yy92;
	if(yych == 'd')	goto yy92;
	goto yy22;
yy89:	yych = *++YYCURSOR;
	if(yych == 'D')	goto yy90;
	if(yych != 'd')	goto yy22;
	goto yy90;
yy90:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy91;
yy91:
{ RET_INSN(addand, 0x40); }
yy92:	yych = *++YYCURSOR;
	if(yych >= '\001')	goto yy21;
	goto yy93;
yy93:
{ RET_INSN(addand, 0x00); }
}


done:
    id_insn = yasm_xmalloc(sizeof(lc3b_id_insn));
    yasm_insn_initialize(&id_insn->insn);
    id_insn->group = group;
    id_insn->mod_data = mod;
    id_insn->num_info = nelems;
    *bc = yasm_bc_create_common(&lc3b_id_insn_callback, id_insn, line);
    return YASM_ARCH_INSN;
}

static void
lc3b_id_insn_destroy(void *contents)
{
    lc3b_id_insn *id_insn = (lc3b_id_insn *)contents;
    yasm_insn_delete(&id_insn->insn, yasm_lc3b__ea_destroy);
    yasm_xfree(contents);
}

static void
lc3b_id_insn_print(const void *contents, FILE *f, int indent_level)
{
    const lc3b_id_insn *id_insn = (const lc3b_id_insn *)contents;
    yasm_insn_print(&id_insn->insn, f, indent_level);
    /*TODO*/
}

/*@only@*/ yasm_bytecode *
yasm_lc3b__create_empty_insn(yasm_arch *arch, unsigned long line)
{
    lc3b_id_insn *id_insn = yasm_xmalloc(sizeof(lc3b_id_insn));

    yasm_insn_initialize(&id_insn->insn);
    id_insn->group = empty_insn;
    id_insn->mod_data = 0;
    id_insn->num_info = NELEMS(empty_insn);

    return yasm_bc_create_common(&lc3b_id_insn_callback, id_insn, line);
}

