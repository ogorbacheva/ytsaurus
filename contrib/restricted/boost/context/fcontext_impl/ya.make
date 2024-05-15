# Generated by devtools/yamaker.

LIBRARY()

WITHOUT_LICENSE_TEXTS()

VERSION(1.85.0)

ORIGINAL_SOURCE(https://github.com/boostorg/context/archive/boost-1.85.0.tar.gz)

LICENSE(BSL-1.0)

SET(BOOST_CONTEXT_ABI sysv)

SET(BOOST_CONTEXT_ARCHITECTURE x86_64)

SET(BOOST_CONTEXT_ASM_EXT .S)

SET(BOOST_CONTEXT_ASSEMBLER gas)

SET(BOOST_CONTEXT_BINARY_FORMAT elf)

PEERDIR(
    contrib/restricted/boost/context/impl_common
)

NO_COMPILER_WARNINGS()

NO_UTIL()

IF (OS_WINDOWS)
    IF (DYNAMIC_BOOST)
        MASMFLAGS(/DBOOST_CONTEXT_EXPORT=EXPORT)
    ELSE()
        MASMFLAGS(/DBOOST_CONTEXT_EXPORT=)
    ENDIF()
ENDIF()

SRCDIR(contrib/restricted/boost/context/src)

IF (OS_DARWIN OR OS_IOS)
    SET(BOOST_CONTEXT_BINARY_FORMAT macho)
ELSEIF (OS_WINDOWS)
    SET(BOOST_CONTEXT_ABI ms)
    SET(BOOST_CONTEXT_ASM_EXT .masm)
    SET(BOOST_CONTEXT_ASSEMBLER masm)
    SET(BOOST_CONTEXT_BINARY_FORMAT pe)
    IF (ARCH_ARM)
        SET(BOOST_CONTEXT_ASSEMBLER armasm)
    ELSEIF (ARCH_I386)
        MASMFLAGS(/safeseh)
    ENDIF()
ENDIF()

IF (ARCH_ARM)
    SET(BOOST_CONTEXT_ABI aapcs)
ENDIF()

IF (ARCH_ARM64)
    SET(BOOST_CONTEXT_ARCHITECTURE arm64)
ELSEIF (ARCH_ARM7)
    SET(BOOST_CONTEXT_ARCHITECTURE arm)
ELSEIF (ARCH_I386)
    SET(BOOST_CONTEXT_ARCHITECTURE i386)
ENDIF()

SET(BOOST_CONTEXT_ASM_SUFFIX      ${BOOST_CONTEXT_ARCHITECTURE}_${BOOST_CONTEXT_ABI}_${BOOST_CONTEXT_BINARY_FORMAT}_${BOOST_CONTEXT_ASSEMBLER}${BOOST_CONTEXT_ASM_EXT})

SRCS(
    asm/jump_${BOOST_CONTEXT_ASM_SUFFIX}
    asm/make_${BOOST_CONTEXT_ASM_SUFFIX}
    asm/ontop_${BOOST_CONTEXT_ASM_SUFFIX}
)

END()
