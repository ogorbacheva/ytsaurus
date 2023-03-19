/* This file auto-generated from standard.mac by genmacro.c - don't edit it */

#include <stddef.h>

static const char *win64_nasm_stdmac[] = {
    "%imacro export 1+.nolist",
    "[export %1]",
    "%endmacro",
    "%imacro proc_frame 1+.nolist",
    "%1:",
    "[proc_frame %1]",
    "%endmacro",
    "%imacro endproc_frame 0.nolist",
    "[endproc_frame]",
    "%endmacro",
    "%imacro push_reg 1",
    "push %1",
    "[pushreg %1]",
    "%endmacro",
    "%imacro rex_push_reg 1",
    "db 0x48",
    "push %1",
    "[pushreg %1]",
    "%endmacro",
    "%imacro push_eflags 0",
    "pushfq",
    "[allocstack 8]",
    "%endmacro",
    "%imacro rex_push_eflags 0",
    "db 0x48",
    "pushfq",
    "[allocstack 8]",
    "%endmacro",
    "%imacro alloc_stack 1",
    "sub rsp, %1",
    "[allocstack %1]",
    "%endmacro",
    "%imacro save_reg 2",
    "mov [rsp+%2], %1",
    "[savereg %1 %2]",
    "%endmacro",
    "%imacro save_xmm128 2",
    "movdqa [rsp+%2], %1",
    "[savexmm128 %1 %2]",
    "%endmacro",
    "%imacro push_frame 0-1.nolist",
    "[pushframe %1]",
    "%endmacro",
    "%imacro set_frame 1-2",
    "%if %0==1",
    "mov %1, rsp",
    "%else",
    "lea %1, [rsp+%2]",
    "%endif",
    "[setframe %1 %2]",
    "%endmacro",
    "%imacro end_prolog 0.nolist",
    "[endprolog]",
    "%endmacro",
    "%imacro end_prologue 0.nolist",
    "[endprolog]",
    "%endmacro",
    NULL
};

