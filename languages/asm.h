char *ASM_HL_extensions[];
char *ASM_HL_keywords[];

const struct editorSyntax syntax_asm = {
    ASM_HL_extensions,
    ASM_HL_keywords,
    "; ","/*","*/",
    HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS  
};

char *ASM_HL_extensions[] = { ".asm",".s", NULL };
char *ASM_HL_keywords[] = {

    /* Nasm registers & argument indexes */
        "%0", "%1", "%2", "%3", "%4", "%5", "%6", "%7", "%8",
        "al", "ah", "ax", "eax", "rax",
        "bl", "bh", "bx", "ebx", "rbx",
        "cl", "ch", "cx", "ecx", "rcx",
        "dl", "dh", "dx", "edx", "rdx",
        "si", "esi", "rsi",
        "di", "edi", "rdi",
        "bp", "ebp", "rbp",
        "sp", "esp", "rsp",
        "r8b", "r8w", "r8d", "r8",
        "r9b", "r9w", "r9d", "r9",
        "r10b", "r10w", "r10d", "r10",
        "r11b", "r11w", "r11d", "r11",
        "r12b", "r12w", "r12d", "r12",
        "r13b", "r13w", "r13d", "r13",
        "r14b", "r14w", "r14d", "r14",
        "r15b", "r15w", "r15d", "r15",
        "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
        "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
        "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",
        "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
        "cr0", "cr2", "cr3", "cr4", "cr8",
        "dr0", "dr1", "dr2", "dr3", "dr6", "dr7",
        "tr3", "tr4", "tr5", "tr6", "tr7",
        "es", "cs", "ss", "ds", "fs", "gs",
        "rip", "eip", "ip",
        "flags", "eflags", "rflags",

    /* Nasm preprocessor */
        "%include",
        "%define", "%undef", "%line", "%idefine", "%xdefine", "%ixdefine",
        "%macro", "%endmacro", "%unmacro", "%assign",
        "%if", "%ifdef", "%ifndef", "%ifnidn", "%ifdefalias", "%ifmacro", "%elif", "%else", "%endif",
        "%defstr", "%deftok", "%defalias", "%eval", "%ord", "%%", "%rotate",
        "%rep", "%endrep", "%clear",
        "extern",

    /* Nasm pseudo-instructions, append | */
        "db|", "dw|", "dd|", "dq|", "dt|", "do|", "dy|", "dz|",
        "resb|", "resw|", "resd|", "resq|", "rest|", "reso|", "resy|", "resz|",
        "incbin|", "equ|", "times|",
        "$|", "$$|",


    /* Nasm true instructions */
        "aaa|", "aad|", "aam|", "aas|", 
        "adc|", "add|", "and|", 
        "arpl|", 
        "bound|", "bsf|", "bsr|", "bswap|", "bt|", "btc|", "btr|", "bts|", 
        "call|", "cbw|", "cdq|", "clc|", "cld|", "cli|", "clts|", "cmc|", "cmp|", "cmpsb|", "cmpsw|", "cmpxchg|", 
        "cpuid|", "cwd|", "cwde|",
        "daa|", "das|", 
        "dec|", "div|", 
        "emms|", "enter|", "esc|", 
        "f2xm1|", "fabs|", "fadd|", "faddp|", "fbld|", "fbstp|", "fchs|", "fclex|", "fcom|", "fcomp|", "fcompp|", "fcos|", "fdecstp|", "fdiv|", "fdivp|", "fdivr|", "fdivrp|", "ffree|", "fiadd|", "ficom|", "ficomp|", "fidiv|", "fidivr|", "fild|", "fimul|", "fincstp|", "finit|", "fist|", "fistp|", "fisttp|", "fisub|", "fisubr|", "fld|", "fld1|", "fldcw|", "fldenv|", "fldl2e|", "fldl2t|", "fldlg2|", "fldln2|", "fldpi|", "fldz|", "fmul|", "fmulp|", "fnclex|", "fndisi|", "fninit|", "fnop|", "fnsave|", "fnstcw|", "fnstenv|", "fnstsw|", "fpatan|", "fprem|", "fprem1|", "fptan|", "frndint|", "frstor|", "fsave|", "fscale|", "fsetpm|", "fsin|", "fsincos|", "fsqrt|", "fst|", "fstcw|", "fstenv|", "fstp|", "fstsw|", "fsub|", "fsubp|", "fsubr|", "fsubrp|", "ftst|", "fucom|", "fucomp|", "fucompp|", "fxam|", "fxch|", "fxtract|", "fyl2x|", "fyl2xp1|",
        "hlt|", "idiv|", "imul|", "in|", "inc|", "insb|", "insw|", "int|", "into|", "invd|", "invlpg|", "iret|", "iretd|", "ja|", "jae|", "jb|", "jbe|", "jc|", "jcxz|", "je|", "jecxz|", "jg|", "jge|", "jl|", "jle|", "jmp|", "jna|", "jnae|", "jnb|", "jnbe|", "jnc|", "jne|", "jng|", "jnge|", "jnl|", "jnle|", "jno|", "jnp|", "jns|", "jnz|", "jo|", "jp|", "jpe|", "jpo|", "js|", "jz|", 
        "lahf|", "lar|", "lds|", "lea|", "leave|", "les|", "lfence|", "lgdt|", "lidt|", "lldt|", "lmsw|", "lock|", "lodsb|", "lodsw|", "loop|", "loope|", "loopne|", "lsl|", "ltr|", 
        "mov|", "movsb|", "movsw|", "movsx|", "movzx|", "mul|", 
        "neg|", "nop|", 
        "not|", "or|", 
        "out|", "outsb|", "outsw|", 
        "pop|", "popa|", "popa|", "popf|", "push|", "pusha|", "pushf|", 
        "rcl|", "rcr|", "rdmsr|", "rdtsc|", "rdtscp|", "rep|", "repe|", "repne|", "repnz|", "repz|", "ret|", "rol|", "ror|", "rsqrtps|", "rsm|", "sahf|", "sal|", "sar|", "sbb|", "scasb|", "scasw|", "setcc|", "sgdt|", "shl|", "shld|", "shr|", "shrd|", "sidt|", "sldt|", "smsw|", "stc|", "std|", "sti|", "stosb|", "stosw|", "str|", "sub|", "syscall|", "sysenter|", "sysexit|", "sysret|", "test|", "ud2|", "verw|", "wait|", "wbinvd|", "xadd|", "xrelease|", "xacquire|", "xchg|", "xlat|", "xor|",

    /* Nasm simd instructions */
        "addpd|", "addps|", "addsd|", "addss|", 
        "andnpd|", "andnps|", "andpd|", "andps|", 
        "blendpd|", "blendps|", "blendvpd|", "blendvps|", 
        "castpd128|", "castpd256|", "castps128|", "castps256|", 
        "clflush|", "cmppd|", "cmpps|", "cmpsd|", "cmpss|", "cmpxchg16b|", 
        "cvtdq2pd|", "cvtdq2ps|", "cvtpd2dq|", "cvtpd2ps|", "cvtps2dq|", "cvtps2pd|", "cvtsd2si|", "cvtsd2ss|", "cvtsi2sd|", "cvtsi2ss|", "cvtss2sd|", "cvtss2si|", "cvttpd2dq|", "cvttpd2pi|", "cvttps2dq|", "cvttps2pi|", "cvttsd2si|", "cvttss2si|", 
        "divpd|", "divps|", "divsd|", "divss|", 
        "dppd|", "dpps|", 
        "emms|", "extractps|", "extrq|", 
        "insertps|", "insertq|", 
        "maskmovdqu|", "maskmovq|", 
        "maxpd|", "maxps|", "maxsd|", "maxss|", "minpd|", "minps|", "minsd|", "minss|", 
        "monitor|", "movapd|", "movaps|", "movd|", "movddup|", "movdq2q|", "movdqa|", "movdqu|", "movhlps|", "movhpd|", "movhps|", "movlhps|", "movlpd|", "movlps|", "movmskpd|", "movmskps|", "movntdq|", "movntdqa|", "movntpd|", "movntps|", "movq|", "movq2dq|", "movsd|", "movshdup|", "movsldup|", "movss|", "movupd|", "movups|", 
        "mulpd|", "mulps|", "mulsd|", "mulss|", 
        "orpd|", "orps|", 
        "pabsb|", "pabsd|", "pabsw|", "packssdw|", "packsswb|", "packusdw|", "packuswb|", "paddb|", "paddd|", "paddq|", "paddsb|", "paddsw|", "paddusb|", "paddusw|", "paddw|", "palignr|", "pand|", "pandn|", "pavgw|", "pavgb|", "pavgusb|", "pcmpeqb|", "pcmpeqd|", "pcmpeqw|", "pcmpeistri|", "pcmpeistrm|", "pcmpgtb|", "pcmpgtd|", "pcmpgtw|", "pcmpistri|", "pcmpistrm|", "pinsrb|", "pinsrd|", "pinsrq|", "pinsrw|", "pmaddwd|", "pmaxsb|", "pmaxsd|", "pmaxsw|", "pmaxub|", "pmaxud|", "pmaxuw|", "pminsb|", "pminsd|", "pminsw|", "pminub|", "pminud|", "pminuw|", "pmovmskb|", "pmovsxbd|", "pmovsxwq|", "pmovsxbq|", "pmovsxwq|", "pmovsxwd|", "pmovsxdq|", "pmovzxbd|", "pmovzxbq|", "pmovzxwq|", "pmovzxwd|", "pmovzxdq|", "pmuldq|", "pmulhrsw|", "pmulhuw|", "pmulhw|", "pmulld|", "pmullw|", "pmuludq|", "popad|", "popfd|", "por|", "psadbw|", "pshufb|", "pshufd|", "pshufhw|", "pshuflw|", "pshufw|", "psignb|", "psignd|", "psignw|", "pslld|", "pslldq|", "psllq|", "psllw|", "psrad|", "psraw|", "psrld|", "psrldq|", "psrlq|", "psrlw|", "psubb|", "psubd|", "psubq|", "psubsb|", "psubsw|", "psubusb|", "psubusw|", "psubw|", "punpckhbw|", "punpckhdq|", "punpckhqdq|", "punpckhwd|", "punpcklbw|", "punpckldq|", "punpcklqdq|", "punpcklwd|", "pxor|", 
        "rcpps|", "rcpss|", "rmpps|", "roundsd|", "roundss|", 
        "rsqrtps|", "rsqrtss|", 
        "sqrtpd|", "sqrtps|", "sqrtsd|", "sqrtss|", 
        "subpd|", "subps|", "subsd|", "subss|", 
        "testpd|", "testps|", 
        "unpckhpd|", "unpckhps|", "unpcklpd|", "unpcklps|", 
        "xorpd|", "xorps|",

        NULL

};