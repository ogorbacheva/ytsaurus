//===-- Host.cpp - Implement OS Host Concept --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the operating system Host concept.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Host.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/X86TargetParser.h"
#include "llvm/Support/raw_ostream.h"
#include <assert.h>
#include <string.h>

// Include the platform-specific parts of this class.
#ifdef LLVM_ON_UNIX
#include "Unix/Host.inc"
#include <sched.h>
#endif
#ifdef _WIN32
#include "Windows/Host.inc"
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif
#if defined(__APPLE__) && (!defined(__x86_64__))
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/machine.h>
#endif

#define DEBUG_TYPE "host-detection"

//===----------------------------------------------------------------------===//
//
//  Implementations of the CPU detection routines
//
//===----------------------------------------------------------------------===//

using namespace llvm;

static std::unique_ptr<llvm::MemoryBuffer>
    LLVM_ATTRIBUTE_UNUSED getProcCpuinfoContent() {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
      llvm::MemoryBuffer::getFileAsStream("/proc/cpuinfo");
  if (std::error_code EC = Text.getError()) {
    llvm::errs() << "Can't read "
                 << "/proc/cpuinfo: " << EC.message() << "\n";
    return nullptr;
  }
  return std::move(*Text);
}

StringRef sys::detail::getHostCPUNameForPowerPC(StringRef ProcCpuinfoContent) {
  // Access to the Processor Version Register (PVR) on PowerPC is privileged,
  // and so we must use an operating-system interface to determine the current
  // processor type. On Linux, this is exposed through the /proc/cpuinfo file.
  const char *generic = "generic";

  // The cpu line is second (after the 'processor: 0' line), so if this
  // buffer is too small then something has changed (or is wrong).
  StringRef::const_iterator CPUInfoStart = ProcCpuinfoContent.begin();
  StringRef::const_iterator CPUInfoEnd = ProcCpuinfoContent.end();

  StringRef::const_iterator CIP = CPUInfoStart;

  StringRef::const_iterator CPUStart = 0;
  size_t CPULen = 0;

  // We need to find the first line which starts with cpu, spaces, and a colon.
  // After the colon, there may be some additional spaces and then the cpu type.
  while (CIP < CPUInfoEnd && CPUStart == 0) {
    if (CIP < CPUInfoEnd && *CIP == '\n')
      ++CIP;

    if (CIP < CPUInfoEnd && *CIP == 'c') {
      ++CIP;
      if (CIP < CPUInfoEnd && *CIP == 'p') {
        ++CIP;
        if (CIP < CPUInfoEnd && *CIP == 'u') {
          ++CIP;
          while (CIP < CPUInfoEnd && (*CIP == ' ' || *CIP == '\t'))
            ++CIP;

          if (CIP < CPUInfoEnd && *CIP == ':') {
            ++CIP;
            while (CIP < CPUInfoEnd && (*CIP == ' ' || *CIP == '\t'))
              ++CIP;

            if (CIP < CPUInfoEnd) {
              CPUStart = CIP;
              while (CIP < CPUInfoEnd && (*CIP != ' ' && *CIP != '\t' &&
                                          *CIP != ',' && *CIP != '\n'))
                ++CIP;
              CPULen = CIP - CPUStart;
            }
          }
        }
      }
    }

    if (CPUStart == 0)
      while (CIP < CPUInfoEnd && *CIP != '\n')
        ++CIP;
  }

  if (CPUStart == 0)
    return generic;

  return StringSwitch<const char *>(StringRef(CPUStart, CPULen))
      .Case("604e", "604e")
      .Case("604", "604")
      .Case("7400", "7400")
      .Case("7410", "7400")
      .Case("7447", "7400")
      .Case("7455", "7450")
      .Case("G4", "g4")
      .Case("POWER4", "970")
      .Case("PPC970FX", "970")
      .Case("PPC970MP", "970")
      .Case("G5", "g5")
      .Case("POWER5", "g5")
      .Case("A2", "a2")
      .Case("POWER6", "pwr6")
      .Case("POWER7", "pwr7")
      .Case("POWER8", "pwr8")
      .Case("POWER8E", "pwr8")
      .Case("POWER8NVL", "pwr8")
      .Case("POWER9", "pwr9")
      .Case("POWER10", "pwr10")
      // FIXME: If we get a simulator or machine with the capabilities of
      // mcpu=future, we should revisit this and add the name reported by the
      // simulator/machine.
      .Default(generic);
}

StringRef sys::detail::getHostCPUNameForARM(StringRef ProcCpuinfoContent) {
  // The cpuid register on arm is not accessible from user space. On Linux,
  // it is exposed through the /proc/cpuinfo file.

  // Read 32 lines from /proc/cpuinfo, which should contain the CPU part line
  // in all cases.
  SmallVector<StringRef, 32> Lines;
  ProcCpuinfoContent.split(Lines, "\n");

  // Look for the CPU implementer line.
  StringRef Implementer;
  StringRef Hardware;
  StringRef Part;
  for (unsigned I = 0, E = Lines.size(); I != E; ++I) {
    if (Lines[I].startswith("CPU implementer"))
      Implementer = Lines[I].substr(15).ltrim("\t :");
    if (Lines[I].startswith("Hardware"))
      Hardware = Lines[I].substr(8).ltrim("\t :");
    if (Lines[I].startswith("CPU part"))
      Part = Lines[I].substr(8).ltrim("\t :");
  }

  if (Implementer == "0x41") { // ARM Ltd.
    // MSM8992/8994 may give cpu part for the core that the kernel is running on,
    // which is undeterministic and wrong. Always return cortex-a53 for these SoC.
    if (Hardware.endswith("MSM8994") || Hardware.endswith("MSM8996"))
      return "cortex-a53";


    // The CPU part is a 3 digit hexadecimal number with a 0x prefix. The
    // values correspond to the "Part number" in the CP15/c0 register. The
    // contents are specified in the various processor manuals.
    // This corresponds to the Main ID Register in Technical Reference Manuals.
    // and is used in programs like sys-utils
    return StringSwitch<const char *>(Part)
        .Case("0x926", "arm926ej-s")
        .Case("0xb02", "mpcore")
        .Case("0xb36", "arm1136j-s")
        .Case("0xb56", "arm1156t2-s")
        .Case("0xb76", "arm1176jz-s")
        .Case("0xc08", "cortex-a8")
        .Case("0xc09", "cortex-a9")
        .Case("0xc0f", "cortex-a15")
        .Case("0xc20", "cortex-m0")
        .Case("0xc23", "cortex-m3")
        .Case("0xc24", "cortex-m4")
        .Case("0xd22", "cortex-m55")
        .Case("0xd02", "cortex-a34")
        .Case("0xd04", "cortex-a35")
        .Case("0xd03", "cortex-a53")
        .Case("0xd07", "cortex-a57")
        .Case("0xd08", "cortex-a72")
        .Case("0xd09", "cortex-a73")
        .Case("0xd0a", "cortex-a75")
        .Case("0xd0b", "cortex-a76")
        .Case("0xd0d", "cortex-a77")
        .Case("0xd41", "cortex-a78")
        .Case("0xd44", "cortex-x1")
        .Case("0xd0c", "neoverse-n1")
        .Case("0xd49", "neoverse-n2")
        .Default("generic");
  }

  if (Implementer == "0x42" || Implementer == "0x43") { // Broadcom | Cavium.
    return StringSwitch<const char *>(Part)
      .Case("0x516", "thunderx2t99")
      .Case("0x0516", "thunderx2t99")
      .Case("0xaf", "thunderx2t99")
      .Case("0x0af", "thunderx2t99")
      .Case("0xa1", "thunderxt88")
      .Case("0x0a1", "thunderxt88")
      .Default("generic");
  }

  if (Implementer == "0x46") { // Fujitsu Ltd.
    return StringSwitch<const char *>(Part)
      .Case("0x001", "a64fx")
      .Default("generic");
  }

  if (Implementer == "0x4e") { // NVIDIA Corporation
    return StringSwitch<const char *>(Part)
        .Case("0x004", "carmel")
        .Default("generic");
  }

  if (Implementer == "0x48") // HiSilicon Technologies, Inc.
    // The CPU part is a 3 digit hexadecimal number with a 0x prefix. The
    // values correspond to the "Part number" in the CP15/c0 register. The
    // contents are specified in the various processor manuals.
    return StringSwitch<const char *>(Part)
      .Case("0xd01", "tsv110")
      .Default("generic");

  if (Implementer == "0x51") // Qualcomm Technologies, Inc.
    // The CPU part is a 3 digit hexadecimal number with a 0x prefix. The
    // values correspond to the "Part number" in the CP15/c0 register. The
    // contents are specified in the various processor manuals.
    return StringSwitch<const char *>(Part)
        .Case("0x06f", "krait") // APQ8064
        .Case("0x201", "kryo")
        .Case("0x205", "kryo")
        .Case("0x211", "kryo")
        .Case("0x800", "cortex-a73") // Kryo 2xx Gold
        .Case("0x801", "cortex-a73") // Kryo 2xx Silver
        .Case("0x802", "cortex-a75") // Kryo 3xx Gold
        .Case("0x803", "cortex-a75") // Kryo 3xx Silver
        .Case("0x804", "cortex-a76") // Kryo 4xx Gold
        .Case("0x805", "cortex-a76") // Kryo 4xx/5xx Silver
        .Case("0xc00", "falkor")
        .Case("0xc01", "saphira")
        .Default("generic");
  if (Implementer == "0x53") { // Samsung Electronics Co., Ltd.
    // The Exynos chips have a convoluted ID scheme that doesn't seem to follow
    // any predictive pattern across variants and parts.
    unsigned Variant = 0, Part = 0;

    // Look for the CPU variant line, whose value is a 1 digit hexadecimal
    // number, corresponding to the Variant bits in the CP15/C0 register.
    for (auto I : Lines)
      if (I.consume_front("CPU variant"))
        I.ltrim("\t :").getAsInteger(0, Variant);

    // Look for the CPU part line, whose value is a 3 digit hexadecimal
    // number, corresponding to the PartNum bits in the CP15/C0 register.
    for (auto I : Lines)
      if (I.consume_front("CPU part"))
        I.ltrim("\t :").getAsInteger(0, Part);

    unsigned Exynos = (Variant << 12) | Part;
    switch (Exynos) {
    default:
      // Default by falling through to Exynos M3.
      LLVM_FALLTHROUGH;
    case 0x1002:
      return "exynos-m3";
    case 0x1003:
      return "exynos-m4";
    }
  }

  return "generic";
}

StringRef sys::detail::getHostCPUNameForS390x(StringRef ProcCpuinfoContent) {
  // STIDP is a privileged operation, so use /proc/cpuinfo instead.

  // The "processor 0:" line comes after a fair amount of other information,
  // including a cache breakdown, but this should be plenty.
  SmallVector<StringRef, 32> Lines;
  ProcCpuinfoContent.split(Lines, "\n");

  // Look for the CPU features.
  SmallVector<StringRef, 32> CPUFeatures;
  for (unsigned I = 0, E = Lines.size(); I != E; ++I)
    if (Lines[I].startswith("features")) {
      size_t Pos = Lines[I].find(':');
      if (Pos != StringRef::npos) {
        Lines[I].drop_front(Pos + 1).split(CPUFeatures, ' ');
        break;
      }
    }

  // We need to check for the presence of vector support independently of
  // the machine type, since we may only use the vector register set when
  // supported by the kernel (and hypervisor).
  bool HaveVectorSupport = false;
  for (unsigned I = 0, E = CPUFeatures.size(); I != E; ++I) {
    if (CPUFeatures[I] == "vx")
      HaveVectorSupport = true;
  }

  // Now check the processor machine type.
  for (unsigned I = 0, E = Lines.size(); I != E; ++I) {
    if (Lines[I].startswith("processor ")) {
      size_t Pos = Lines[I].find("machine = ");
      if (Pos != StringRef::npos) {
        Pos += sizeof("machine = ") - 1;
        unsigned int Id;
        if (!Lines[I].drop_front(Pos).getAsInteger(10, Id)) {
          if (Id >= 8561 && HaveVectorSupport)
            return "z15";
          if (Id >= 3906 && HaveVectorSupport)
            return "z14";
          if (Id >= 2964 && HaveVectorSupport)
            return "z13";
          if (Id >= 2827)
            return "zEC12";
          if (Id >= 2817)
            return "z196";
        }
      }
      break;
    }
  }

  return "generic";
}

StringRef sys::detail::getHostCPUNameForBPF() {
#if !defined(__linux__) || !defined(__x86_64__)
  return "generic";
#else
  uint8_t v3_insns[40] __attribute__ ((aligned (8))) =
      /* BPF_MOV64_IMM(BPF_REG_0, 0) */
    { 0xb7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_2, 1) */
      0xb7, 0x2, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_JMP32_REG(BPF_JLT, BPF_REG_0, BPF_REG_2, 1) */
      0xae, 0x20, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_0, 1) */
      0xb7, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_EXIT_INSN() */
      0x95, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

  uint8_t v2_insns[40] __attribute__ ((aligned (8))) =
      /* BPF_MOV64_IMM(BPF_REG_0, 0) */
    { 0xb7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_2, 1) */
      0xb7, 0x2, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_JMP_REG(BPF_JLT, BPF_REG_0, BPF_REG_2, 1) */
      0xad, 0x20, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_0, 1) */
      0xb7, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_EXIT_INSN() */
      0x95, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

  struct bpf_prog_load_attr {
    uint32_t prog_type;
    uint32_t insn_cnt;
    uint64_t insns;
    uint64_t license;
    uint32_t log_level;
    uint32_t log_size;
    uint64_t log_buf;
    uint32_t kern_version;
    uint32_t prog_flags;
  } attr = {};
  attr.prog_type = 1; /* BPF_PROG_TYPE_SOCKET_FILTER */
  attr.insn_cnt = 5;
  attr.insns = (uint64_t)v3_insns;
  attr.license = (uint64_t)"DUMMY";

  int fd = syscall(321 /* __NR_bpf */, 5 /* BPF_PROG_LOAD */, &attr,
                   sizeof(attr));
  if (fd >= 0) {
    close(fd);
    return "v3";
  }

  /* Clear the whole attr in case its content changed by syscall. */
  memset(&attr, 0, sizeof(attr));
  attr.prog_type = 1; /* BPF_PROG_TYPE_SOCKET_FILTER */
  attr.insn_cnt = 5;
  attr.insns = (uint64_t)v2_insns;
  attr.license = (uint64_t)"DUMMY";
  fd = syscall(321 /* __NR_bpf */, 5 /* BPF_PROG_LOAD */, &attr, sizeof(attr));
  if (fd >= 0) {
    close(fd);
    return "v2";
  }
  return "v1";
#endif
}

#if defined(__i386__) || defined(_M_IX86) || \
    defined(__x86_64__) || defined(_M_X64)

// The check below for i386 was copied from clang's cpuid.h (__get_cpuid_max).
// Check motivated by bug reports for OpenSSL crashing on CPUs without CPUID
// support. Consequently, for i386, the presence of CPUID is checked first
// via the corresponding eflags bit.
// Removal of cpuid.h header motivated by PR30384
// Header cpuid.h and method __get_cpuid_max are not used in llvm, clang, openmp
// or test-suite, but are used in external projects e.g. libstdcxx
static bool isCpuIdSupported() {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__i386__)
  int __cpuid_supported;
  __asm__("  pushfl\n"
          "  popl   %%eax\n"
          "  movl   %%eax,%%ecx\n"
          "  xorl   $0x00200000,%%eax\n"
          "  pushl  %%eax\n"
          "  popfl\n"
          "  pushfl\n"
          "  popl   %%eax\n"
          "  movl   $0,%0\n"
          "  cmpl   %%eax,%%ecx\n"
          "  je     1f\n"
          "  movl   $1,%0\n"
          "1:"
          : "=r"(__cpuid_supported)
          :
          : "eax", "ecx");
  if (!__cpuid_supported)
    return false;
#endif
  return true;
#endif
  return true;
}

/// getX86CpuIDAndInfo - Execute the specified cpuid and return the 4 values in
/// the specified arguments.  If we can't run cpuid on the host, return true.
static bool getX86CpuIDAndInfo(unsigned value, unsigned *rEAX, unsigned *rEBX,
                               unsigned *rECX, unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
  // gcc doesn't know cpuid would clobber ebx/rbx. Preserve it manually.
  // FIXME: should we save this for Clang?
  __asm__("movq\t%%rbx, %%rsi\n\t"
          "cpuid\n\t"
          "xchgq\t%%rbx, %%rsi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value));
  return false;
#elif defined(__i386__)
  __asm__("movl\t%%ebx, %%esi\n\t"
          "cpuid\n\t"
          "xchgl\t%%ebx, %%esi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value));
  return false;
#else
  return true;
#endif
#elif defined(_MSC_VER)
  // The MSVC intrinsic is portable across x86 and x64.
  int registers[4];
  __cpuid(registers, value);
  *rEAX = registers[0];
  *rEBX = registers[1];
  *rECX = registers[2];
  *rEDX = registers[3];
  return false;
#else
  return true;
#endif
}

namespace llvm {
namespace sys {
namespace detail {
namespace x86 {

VendorSignatures getVendorSignature(unsigned *MaxLeaf) {
  unsigned EAX = 0, EBX = 0, ECX = 0, EDX = 0;
  if (MaxLeaf == nullptr)
    MaxLeaf = &EAX;
  else
    *MaxLeaf = 0;

  if (!isCpuIdSupported())
    return VendorSignatures::UNKNOWN;

  if (getX86CpuIDAndInfo(0, MaxLeaf, &EBX, &ECX, &EDX) || *MaxLeaf < 1)
    return VendorSignatures::UNKNOWN;

  // "Genu ineI ntel"
  if (EBX == 0x756e6547 && EDX == 0x49656e69 && ECX == 0x6c65746e)
    return VendorSignatures::GENUINE_INTEL;

  // "Auth enti cAMD"
  if (EBX == 0x68747541 && EDX == 0x69746e65 && ECX == 0x444d4163)
    return VendorSignatures::AUTHENTIC_AMD;

  return VendorSignatures::UNKNOWN;
}

} // namespace x86
} // namespace detail
} // namespace sys
} // namespace llvm

using namespace llvm::sys::detail::x86;

/// getX86CpuIDAndInfoEx - Execute the specified cpuid with subleaf and return
/// the 4 values in the specified arguments.  If we can't run cpuid on the host,
/// return true.
static bool getX86CpuIDAndInfoEx(unsigned value, unsigned subleaf,
                                 unsigned *rEAX, unsigned *rEBX, unsigned *rECX,
                                 unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
  // gcc doesn't know cpuid would clobber ebx/rbx. Preserve it manually.
  // FIXME: should we save this for Clang?
  __asm__("movq\t%%rbx, %%rsi\n\t"
          "cpuid\n\t"
          "xchgq\t%%rbx, %%rsi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value), "c"(subleaf));
  return false;
#elif defined(__i386__)
  __asm__("movl\t%%ebx, %%esi\n\t"
          "cpuid\n\t"
          "xchgl\t%%ebx, %%esi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value), "c"(subleaf));
  return false;
#else
  return true;
#endif
#elif defined(_MSC_VER)
  int registers[4];
  __cpuidex(registers, value, subleaf);
  *rEAX = registers[0];
  *rEBX = registers[1];
  *rECX = registers[2];
  *rEDX = registers[3];
  return false;
#else
  return true;
#endif
}

// Read control register 0 (XCR0). Used to detect features such as AVX.
static bool getX86XCR0(unsigned *rEAX, unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
  // Check xgetbv; this uses a .byte sequence instead of the instruction
  // directly because older assemblers do not include support for xgetbv and
  // there is no easy way to conditionally compile based on the assembler used.
  __asm__(".byte 0x0f, 0x01, 0xd0" : "=a"(*rEAX), "=d"(*rEDX) : "c"(0));
  return false;
#elif defined(_MSC_FULL_VER) && defined(_XCR_XFEATURE_ENABLED_MASK)
  unsigned long long Result = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
  *rEAX = Result;
  *rEDX = Result >> 32;
  return false;
#else
  return true;
#endif
}

static void detectX86FamilyModel(unsigned EAX, unsigned *Family,
                                 unsigned *Model) {
  *Family = (EAX >> 8) & 0xf; // Bits 8 - 11
  *Model = (EAX >> 4) & 0xf;  // Bits 4 - 7
  if (*Family == 6 || *Family == 0xf) {
    if (*Family == 0xf)
      // Examine extended family ID if family ID is F.
      *Family += (EAX >> 20) & 0xff; // Bits 20 - 27
    // Examine extended model ID if family ID is 6 or F.
    *Model += ((EAX >> 16) & 0xf) << 4; // Bits 16 - 19
  }
}

static StringRef
getIntelProcessorTypeAndSubtype(unsigned Family, unsigned Model,
                                const unsigned *Features,
                                unsigned *Type, unsigned *Subtype) {
  auto testFeature = [&](unsigned F) {
    return (Features[F / 32] & (1U << (F % 32))) != 0;
  };

  StringRef CPU;

  switch (Family) {
  case 3:
    CPU = "i386";
    break;
  case 4:
    CPU = "i486";
    break;
  case 5:
    if (testFeature(X86::FEATURE_MMX)) {
      CPU = "pentium-mmx";
      break;
    }
    CPU = "pentium";
    break;
  case 6:
    switch (Model) {
    case 0x0f: // Intel Core 2 Duo processor, Intel Core 2 Duo mobile
               // processor, Intel Core 2 Quad processor, Intel Core 2 Quad
               // mobile processor, Intel Core 2 Extreme processor, Intel
               // Pentium Dual-Core processor, Intel Xeon processor, model
               // 0Fh. All processors are manufactured using the 65 nm process.
    case 0x16: // Intel Celeron processor model 16h. All processors are
               // manufactured using the 65 nm process
      CPU = "core2";
      *Type = X86::INTEL_CORE2;
      break;
    case 0x17: // Intel Core 2 Extreme processor, Intel Xeon processor, model
               // 17h. All processors are manufactured using the 45 nm process.
               //
               // 45nm: Penryn , Wolfdale, Yorkfield (XE)
    case 0x1d: // Intel Xeon processor MP. All processors are manufactured using
               // the 45 nm process.
      CPU = "penryn";
      *Type = X86::INTEL_CORE2;
      break;
    case 0x1a: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 45 nm process.
    case 0x1e: // Intel(R) Core(TM) i7 CPU         870  @ 2.93GHz.
               // As found in a Summer 2010 model iMac.
    case 0x1f:
    case 0x2e:              // Nehalem EX
      CPU = "nehalem";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_NEHALEM;
      break;
    case 0x25: // Intel Core i7, laptop version.
    case 0x2c: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 32 nm process.
    case 0x2f: // Westmere EX
      CPU = "westmere";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_WESTMERE;
      break;
    case 0x2a: // Intel Core i7 processor. All processors are manufactured
               // using the 32 nm process.
    case 0x2d:
      CPU = "sandybridge";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_SANDYBRIDGE;
      break;
    case 0x3a:
    case 0x3e:              // Ivy Bridge EP
      CPU = "ivybridge";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_IVYBRIDGE;
      break;

    // Haswell:
    case 0x3c:
    case 0x3f:
    case 0x45:
    case 0x46:
      CPU = "haswell";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_HASWELL;
      break;

    // Broadwell:
    case 0x3d:
    case 0x47:
    case 0x4f:
    case 0x56:
      CPU = "broadwell";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_BROADWELL;
      break;

    // Skylake:
    case 0x4e:              // Skylake mobile
    case 0x5e:              // Skylake desktop
    case 0x8e:              // Kaby Lake mobile
    case 0x9e:              // Kaby Lake desktop
    case 0xa5:              // Comet Lake-H/S
    case 0xa6:              // Comet Lake-U
      CPU = "skylake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_SKYLAKE;
      break;

    // Skylake Xeon:
    case 0x55:
      *Type = X86::INTEL_COREI7;
      if (testFeature(X86::FEATURE_AVX512BF16)) {
        CPU = "cooperlake";
        *Subtype = X86::INTEL_COREI7_COOPERLAKE;
      } else if (testFeature(X86::FEATURE_AVX512VNNI)) {
        CPU = "cascadelake";
        *Subtype = X86::INTEL_COREI7_CASCADELAKE;
      } else {
        CPU = "skylake-avx512";
        *Subtype = X86::INTEL_COREI7_SKYLAKE_AVX512;
      }
      break;

    // Cannonlake:
    case 0x66:
      CPU = "cannonlake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_CANNONLAKE;
      break;

    // Icelake:
    case 0x7d:
    case 0x7e:
      CPU = "icelake-client";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ICELAKE_CLIENT;
      break;

    // Icelake Xeon:
    case 0x6a:
    case 0x6c:
      CPU = "icelake-server";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ICELAKE_SERVER;
      break;

    // Sapphire Rapids:
    case 0x8f:
      CPU = "sapphirerapids";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_SAPPHIRERAPIDS;
      break;

    case 0x1c: // Most 45 nm Intel Atom processors
    case 0x26: // 45 nm Atom Lincroft
    case 0x27: // 32 nm Atom Medfield
    case 0x35: // 32 nm Atom Midview
    case 0x36: // 32 nm Atom Midview
      CPU = "bonnell";
      *Type = X86::INTEL_BONNELL;
      break;

    // Atom Silvermont codes from the Intel software optimization guide.
    case 0x37:
    case 0x4a:
    case 0x4d:
    case 0x5a:
    case 0x5d:
    case 0x4c: // really airmont
      CPU = "silvermont";
      *Type = X86::INTEL_SILVERMONT;
      break;
    // Goldmont:
    case 0x5c: // Apollo Lake
    case 0x5f: // Denverton
      CPU = "goldmont";
      *Type = X86::INTEL_GOLDMONT;
      break;
    case 0x7a:
      CPU = "goldmont-plus";
      *Type = X86::INTEL_GOLDMONT_PLUS;
      break;
    case 0x86:
      CPU = "tremont";
      *Type = X86::INTEL_TREMONT;
      break;

    // Xeon Phi (Knights Landing + Knights Mill):
    case 0x57:
      CPU = "knl";
      *Type = X86::INTEL_KNL;
      break;
    case 0x85:
      CPU = "knm";
      *Type = X86::INTEL_KNM;
      break;

    default: // Unknown family 6 CPU, try to guess.
      // Don't both with Type/Subtype here, they aren't used by the caller.
      // They're used above to keep the code in sync with compiler-rt.
      // TODO detect tigerlake host from model
      if (testFeature(X86::FEATURE_AVX512VP2INTERSECT)) {
        CPU = "tigerlake";
      } else if (testFeature(X86::FEATURE_AVX512VBMI2)) {
        CPU = "icelake-client";
      } else if (testFeature(X86::FEATURE_AVX512VBMI)) {
        CPU = "cannonlake";
      } else if (testFeature(X86::FEATURE_AVX512BF16)) {
        CPU = "cooperlake";
      } else if (testFeature(X86::FEATURE_AVX512VNNI)) {
        CPU = "cascadelake";
      } else if (testFeature(X86::FEATURE_AVX512VL)) {
        CPU = "skylake-avx512";
      } else if (testFeature(X86::FEATURE_AVX512ER)) {
        CPU = "knl";
      } else if (testFeature(X86::FEATURE_CLFLUSHOPT)) {
        if (testFeature(X86::FEATURE_SHA))
          CPU = "goldmont";
        else
          CPU = "skylake";
      } else if (testFeature(X86::FEATURE_ADX)) {
        CPU = "broadwell";
      } else if (testFeature(X86::FEATURE_AVX2)) {
        CPU = "haswell";
      } else if (testFeature(X86::FEATURE_AVX)) {
        CPU = "sandybridge";
      } else if (testFeature(X86::FEATURE_SSE4_2)) {
        if (testFeature(X86::FEATURE_MOVBE))
          CPU = "silvermont";
        else
          CPU = "nehalem";
      } else if (testFeature(X86::FEATURE_SSE4_1)) {
        CPU = "penryn";
      } else if (testFeature(X86::FEATURE_SSSE3)) {
        if (testFeature(X86::FEATURE_MOVBE))
          CPU = "bonnell";
        else
          CPU = "core2";
      } else if (testFeature(X86::FEATURE_64BIT)) {
        CPU = "core2";
      } else if (testFeature(X86::FEATURE_SSE3)) {
        CPU = "yonah";
      } else if (testFeature(X86::FEATURE_SSE2)) {
        CPU = "pentium-m";
      } else if (testFeature(X86::FEATURE_SSE)) {
        CPU = "pentium3";
      } else if (testFeature(X86::FEATURE_MMX)) {
        CPU = "pentium2";
      } else {
        CPU = "pentiumpro";
      }
      break;
    }
    break;
  case 15: {
    if (testFeature(X86::FEATURE_64BIT)) {
      CPU = "nocona";
      break;
    }
    if (testFeature(X86::FEATURE_SSE3)) {
      CPU = "prescott";
      break;
    }
    CPU = "pentium4";
    break;
  }
  default:
    break; // Unknown.
  }

  return CPU;
}

static StringRef
getAMDProcessorTypeAndSubtype(unsigned Family, unsigned Model,
                              const unsigned *Features,
                              unsigned *Type, unsigned *Subtype) {
  auto testFeature = [&](unsigned F) {
    return (Features[F / 32] & (1U << (F % 32))) != 0;
  };

  StringRef CPU;

  switch (Family) {
  case 4:
    CPU = "i486";
    break;
  case 5:
    CPU = "pentium";
    switch (Model) {
    case 6:
    case 7:
      CPU = "k6";
      break;
    case 8:
      CPU = "k6-2";
      break;
    case 9:
    case 13:
      CPU = "k6-3";
      break;
    case 10:
      CPU = "geode";
      break;
    }
    break;
  case 6:
    if (testFeature(X86::FEATURE_SSE)) {
      CPU = "athlon-xp";
      break;
    }
    CPU = "athlon";
    break;
  case 15:
    if (testFeature(X86::FEATURE_SSE3)) {
      CPU = "k8-sse3";
      break;
    }
    CPU = "k8";
    break;
  case 16:
    CPU = "amdfam10";
    *Type = X86::AMDFAM10H; // "amdfam10"
    switch (Model) {
    case 2:
      *Subtype = X86::AMDFAM10H_BARCELONA;
      break;
    case 4:
      *Subtype = X86::AMDFAM10H_SHANGHAI;
      break;
    case 8:
      *Subtype = X86::AMDFAM10H_ISTANBUL;
      break;
    }
    break;
  case 20:
    CPU = "btver1";
    *Type = X86::AMD_BTVER1;
    break;
  case 21:
    CPU = "bdver1";
    *Type = X86::AMDFAM15H;
    if (Model >= 0x60 && Model <= 0x7f) {
      CPU = "bdver4";
      *Subtype = X86::AMDFAM15H_BDVER4;
      break; // 60h-7Fh: Excavator
    }
    if (Model >= 0x30 && Model <= 0x3f) {
      CPU = "bdver3";
      *Subtype = X86::AMDFAM15H_BDVER3;
      break; // 30h-3Fh: Steamroller
    }
    if ((Model >= 0x10 && Model <= 0x1f) || Model == 0x02) {
      CPU = "bdver2";
      *Subtype = X86::AMDFAM15H_BDVER2;
      break; // 02h, 10h-1Fh: Piledriver
    }
    if (Model <= 0x0f) {
      *Subtype = X86::AMDFAM15H_BDVER1;
      break; // 00h-0Fh: Bulldozer
    }
    break;
  case 22:
    CPU = "btver2";
    *Type = X86::AMD_BTVER2;
    break;
  case 23:
    CPU = "znver1";
    *Type = X86::AMDFAM17H;
    if ((Model >= 0x30 && Model <= 0x3f) || Model == 0x71) {
      CPU = "znver2";
      *Subtype = X86::AMDFAM17H_ZNVER2;
      break; // 30h-3fh, 71h: Zen2
    }
    if (Model <= 0x0f) {
      *Subtype = X86::AMDFAM17H_ZNVER1;
      break; // 00h-0Fh: Zen1
    }
    break;
  case 25:
    CPU = "znver3";
    *Type = X86::AMDFAM19H;
    if (Model <= 0x0f) {
      *Subtype = X86::AMDFAM19H_ZNVER3;
      break; // 00h-0Fh: Zen3
    }
    break;
  default:
    break; // Unknown AMD CPU.
  }

  return CPU;
}

static void getAvailableFeatures(unsigned ECX, unsigned EDX, unsigned MaxLeaf,
                                 unsigned *Features) {
  unsigned EAX, EBX;

  auto setFeature = [&](unsigned F) {
    Features[F / 32] |= 1U << (F % 32);
  };

  if ((EDX >> 15) & 1)
    setFeature(X86::FEATURE_CMOV);
  if ((EDX >> 23) & 1)
    setFeature(X86::FEATURE_MMX);
  if ((EDX >> 25) & 1)
    setFeature(X86::FEATURE_SSE);
  if ((EDX >> 26) & 1)
    setFeature(X86::FEATURE_SSE2);

  if ((ECX >> 0) & 1)
    setFeature(X86::FEATURE_SSE3);
  if ((ECX >> 1) & 1)
    setFeature(X86::FEATURE_PCLMUL);
  if ((ECX >> 9) & 1)
    setFeature(X86::FEATURE_SSSE3);
  if ((ECX >> 12) & 1)
    setFeature(X86::FEATURE_FMA);
  if ((ECX >> 19) & 1)
    setFeature(X86::FEATURE_SSE4_1);
  if ((ECX >> 20) & 1)
    setFeature(X86::FEATURE_SSE4_2);
  if ((ECX >> 23) & 1)
    setFeature(X86::FEATURE_POPCNT);
  if ((ECX >> 25) & 1)
    setFeature(X86::FEATURE_AES);

  if ((ECX >> 22) & 1)
    setFeature(X86::FEATURE_MOVBE);

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  const unsigned AVXBits = (1 << 27) | (1 << 28);
  bool HasAVX = ((ECX & AVXBits) == AVXBits) && !getX86XCR0(&EAX, &EDX) &&
                ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVX && ((EAX & 0xe0) == 0xe0);
#endif

  if (HasAVX)
    setFeature(X86::FEATURE_AVX);

  bool HasLeaf7 =
      MaxLeaf >= 0x7 && !getX86CpuIDAndInfoEx(0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  if (HasLeaf7 && ((EBX >> 3) & 1))
    setFeature(X86::FEATURE_BMI);
  if (HasLeaf7 && ((EBX >> 5) & 1) && HasAVX)
    setFeature(X86::FEATURE_AVX2);
  if (HasLeaf7 && ((EBX >> 8) & 1))
    setFeature(X86::FEATURE_BMI2);
  if (HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512F);
  if (HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512DQ);
  if (HasLeaf7 && ((EBX >> 19) & 1))
    setFeature(X86::FEATURE_ADX);
  if (HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512IFMA);
  if (HasLeaf7 && ((EBX >> 23) & 1))
    setFeature(X86::FEATURE_CLFLUSHOPT);
  if (HasLeaf7 && ((EBX >> 26) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512PF);
  if (HasLeaf7 && ((EBX >> 27) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512ER);
  if (HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512CD);
  if (HasLeaf7 && ((EBX >> 29) & 1))
    setFeature(X86::FEATURE_SHA);
  if (HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512BW);
  if (HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VL);

  if (HasLeaf7 && ((ECX >> 1) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VBMI);
  if (HasLeaf7 && ((ECX >> 6) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VBMI2);
  if (HasLeaf7 && ((ECX >> 8) & 1))
    setFeature(X86::FEATURE_GFNI);
  if (HasLeaf7 && ((ECX >> 10) & 1) && HasAVX)
    setFeature(X86::FEATURE_VPCLMULQDQ);
  if (HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VNNI);
  if (HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512BITALG);
  if (HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VPOPCNTDQ);

  if (HasLeaf7 && ((EDX >> 2) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX5124VNNIW);
  if (HasLeaf7 && ((EDX >> 3) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX5124FMAPS);
  if (HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VP2INTERSECT);

  bool HasLeaf7Subleaf1 =
      MaxLeaf >= 7 && !getX86CpuIDAndInfoEx(0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512BF16);

  unsigned MaxExtLevel;
  getX86CpuIDAndInfo(0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !getX86CpuIDAndInfo(0x80000001, &EAX, &EBX, &ECX, &EDX);
  if (HasExtLeaf1 && ((ECX >> 6) & 1))
    setFeature(X86::FEATURE_SSE4_A);
  if (HasExtLeaf1 && ((ECX >> 11) & 1))
    setFeature(X86::FEATURE_XOP);
  if (HasExtLeaf1 && ((ECX >> 16) & 1))
    setFeature(X86::FEATURE_FMA4);

  if (HasExtLeaf1 && ((EDX >> 29) & 1))
    setFeature(X86::FEATURE_64BIT);
}

StringRef sys::getHostCPUName() {
  unsigned MaxLeaf = 0;
  const VendorSignatures Vendor = getVendorSignature(&MaxLeaf);
  if (Vendor == VendorSignatures::UNKNOWN)
    return "generic";

  unsigned EAX = 0, EBX = 0, ECX = 0, EDX = 0;
  getX86CpuIDAndInfo(0x1, &EAX, &EBX, &ECX, &EDX);

  unsigned Family = 0, Model = 0;
  unsigned Features[(X86::CPU_FEATURE_MAX + 31) / 32] = {0};
  detectX86FamilyModel(EAX, &Family, &Model);
  getAvailableFeatures(ECX, EDX, MaxLeaf, Features);

  // These aren't consumed in this file, but we try to keep some source code the
  // same or similar to compiler-rt.
  unsigned Type = 0;
  unsigned Subtype = 0;

  StringRef CPU;

  if (Vendor == VendorSignatures::GENUINE_INTEL) {
    CPU = getIntelProcessorTypeAndSubtype(Family, Model, Features, &Type,
                                          &Subtype);
  } else if (Vendor == VendorSignatures::AUTHENTIC_AMD) {
    CPU = getAMDProcessorTypeAndSubtype(Family, Model, Features, &Type,
                                        &Subtype);
  }

  if (!CPU.empty())
    return CPU;

  return "generic";
}

#elif defined(__APPLE__) && (defined(__ppc__) || defined(__powerpc__))
StringRef sys::getHostCPUName() {
  host_basic_info_data_t hostInfo;
  mach_msg_type_number_t infoCount;

  infoCount = HOST_BASIC_INFO_COUNT;
  mach_port_t hostPort = mach_host_self();
  host_info(hostPort, HOST_BASIC_INFO, (host_info_t)&hostInfo,
            &infoCount);
  mach_port_deallocate(mach_task_self(), hostPort);

  if (hostInfo.cpu_type != CPU_TYPE_POWERPC)
    return "generic";

  switch (hostInfo.cpu_subtype) {
  case CPU_SUBTYPE_POWERPC_601:
    return "601";
  case CPU_SUBTYPE_POWERPC_602:
    return "602";
  case CPU_SUBTYPE_POWERPC_603:
    return "603";
  case CPU_SUBTYPE_POWERPC_603e:
    return "603e";
  case CPU_SUBTYPE_POWERPC_603ev:
    return "603ev";
  case CPU_SUBTYPE_POWERPC_604:
    return "604";
  case CPU_SUBTYPE_POWERPC_604e:
    return "604e";
  case CPU_SUBTYPE_POWERPC_620:
    return "620";
  case CPU_SUBTYPE_POWERPC_750:
    return "750";
  case CPU_SUBTYPE_POWERPC_7400:
    return "7400";
  case CPU_SUBTYPE_POWERPC_7450:
    return "7450";
  case CPU_SUBTYPE_POWERPC_970:
    return "970";
  default:;
  }

  return "generic";
}
#elif defined(__linux__) && (defined(__ppc__) || defined(__powerpc__))
StringRef sys::getHostCPUName() {
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  return detail::getHostCPUNameForPowerPC(Content);
}
#elif defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
StringRef sys::getHostCPUName() {
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  return detail::getHostCPUNameForARM(Content);
}
#elif defined(__linux__) && defined(__s390x__)
StringRef sys::getHostCPUName() {
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  return detail::getHostCPUNameForS390x(Content);
}
#elif defined(__APPLE__) && defined(__aarch64__)
StringRef sys::getHostCPUName() {
  return "cyclone";
}
#elif defined(__APPLE__) && defined(__arm__)
StringRef sys::getHostCPUName() {
  host_basic_info_data_t hostInfo;
  mach_msg_type_number_t infoCount;

  infoCount = HOST_BASIC_INFO_COUNT;
  mach_port_t hostPort = mach_host_self();
  host_info(hostPort, HOST_BASIC_INFO, (host_info_t)&hostInfo,
            &infoCount);
  mach_port_deallocate(mach_task_self(), hostPort);

  if (hostInfo.cpu_type != CPU_TYPE_ARM) {
    assert(false && "CPUType not equal to ARM should not be possible on ARM");
    return "generic";
  }
  switch (hostInfo.cpu_subtype) {
    case CPU_SUBTYPE_ARM_V7S:
      return "swift";
    default:;
    }

  return "generic";
}
#else
StringRef sys::getHostCPUName() { return "generic"; }
namespace llvm {
namespace sys {
namespace detail {
namespace x86 {

VendorSignatures getVendorSignature(unsigned *MaxLeaf) {
  return VendorSignatures::UNKNOWN;
}

} // namespace x86
} // namespace detail
} // namespace sys
} // namespace llvm
#endif

#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
// On Linux, the number of physical cores can be computed from /proc/cpuinfo,
// using the number of unique physical/core id pairs. The following
// implementation reads the /proc/cpuinfo format on an x86_64 system.
int computeHostNumPhysicalCores() {
  // Enabled represents the number of physical id/core id pairs with at least
  // one processor id enabled by the CPU affinity mask.
  cpu_set_t Affinity, Enabled;
  if (sched_getaffinity(0, sizeof(Affinity), &Affinity) != 0)
    return -1;
  CPU_ZERO(&Enabled);

  // Read /proc/cpuinfo as a stream (until EOF reached). It cannot be
  // mmapped because it appears to have 0 size.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
      llvm::MemoryBuffer::getFileAsStream("/proc/cpuinfo");
  if (std::error_code EC = Text.getError()) {
    llvm::errs() << "Can't read "
                 << "/proc/cpuinfo: " << EC.message() << "\n";
    return -1;
  }
  SmallVector<StringRef, 8> strs;
  (*Text)->getBuffer().split(strs, "\n", /*MaxSplit=*/-1,
                             /*KeepEmpty=*/false);
  int CurProcessor = -1;
  int CurPhysicalId = -1;
  int CurSiblings = -1;
  int CurCoreId = -1;
  for (StringRef Line : strs) {
    std::pair<StringRef, StringRef> Data = Line.split(':');
    auto Name = Data.first.trim();
    auto Val = Data.second.trim();
    // These fields are available if the kernel is configured with CONFIG_SMP.
    if (Name == "processor")
      Val.getAsInteger(10, CurProcessor);
    else if (Name == "physical id")
      Val.getAsInteger(10, CurPhysicalId);
    else if (Name == "siblings")
      Val.getAsInteger(10, CurSiblings);
    else if (Name == "core id") {
      Val.getAsInteger(10, CurCoreId);
      // The processor id corresponds to an index into cpu_set_t.
      if (CPU_ISSET(CurProcessor, &Affinity))
        CPU_SET(CurPhysicalId * CurSiblings + CurCoreId, &Enabled);
    }
  }
  return CPU_COUNT(&Enabled);
}
#elif defined(__linux__) && defined(__powerpc__)
int computeHostNumPhysicalCores() {
  cpu_set_t Affinity;
  if (sched_getaffinity(0, sizeof(Affinity), &Affinity) == 0)
    return CPU_COUNT(&Affinity);

  // The call to sched_getaffinity() may have failed because the Affinity
  // mask is too small for the number of CPU's on the system (i.e. the
  // system has more than 1024 CPUs). Allocate a mask large enough for
  // twice as many CPUs.
  cpu_set_t *DynAffinity;
  DynAffinity = CPU_ALLOC(2048);
  if (sched_getaffinity(0, CPU_ALLOC_SIZE(2048), DynAffinity) == 0) {
    int NumCPUs = CPU_COUNT(DynAffinity);
    CPU_FREE(DynAffinity);
    return NumCPUs;
  }
  return -1;
}
#elif defined(__linux__) && defined(__s390x__)
int computeHostNumPhysicalCores() { return sysconf(_SC_NPROCESSORS_ONLN); }
#elif defined(__APPLE__) && defined(__x86_64__)
#include <sys/param.h>
#include <sys/sysctl.h>

// Gets the number of *physical cores* on the machine.
int computeHostNumPhysicalCores() {
  uint32_t count;
  size_t len = sizeof(count);
  sysctlbyname("hw.physicalcpu", &count, &len, NULL, 0);
  if (count < 1) {
    int nm[2];
    nm[0] = CTL_HW;
    nm[1] = HW_AVAILCPU;
    sysctl(nm, 2, &count, &len, NULL, 0);
    if (count < 1)
      return -1;
  }
  return count;
}
#elif defined(__MVS__)
int computeHostNumPhysicalCores() {
  enum {
    // Byte offset of the pointer to the Communications Vector Table (CVT) in
    // the Prefixed Save Area (PSA). The table entry is a 31-bit pointer and
    // will be zero-extended to uintptr_t.
    FLCCVT = 16,
    // Byte offset of the pointer to the Common System Data Area (CSD) in the
    // CVT. The table entry is a 31-bit pointer and will be zero-extended to
    // uintptr_t.
    CVTCSD = 660,
    // Byte offset to the number of live CPs in the LPAR, stored as a signed
    // 32-bit value in the table.
    CSD_NUMBER_ONLINE_STANDARD_CPS = 264,
  };
  char *PSA = 0;
  char *CVT = reinterpret_cast<char *>(
      static_cast<uintptr_t>(reinterpret_cast<unsigned int &>(PSA[FLCCVT])));
  char *CSD = reinterpret_cast<char *>(
      static_cast<uintptr_t>(reinterpret_cast<unsigned int &>(CVT[CVTCSD])));
  return reinterpret_cast<int &>(CSD[CSD_NUMBER_ONLINE_STANDARD_CPS]);
}
#elif defined(_WIN32) && LLVM_ENABLE_THREADS != 0
// Defined in llvm/lib/Support/Windows/Threading.inc
int computeHostNumPhysicalCores();
#else
// On other systems, return -1 to indicate unknown.
static int computeHostNumPhysicalCores() { return -1; }
#endif

int sys::getHostNumPhysicalCores() {
  static int NumCores = computeHostNumPhysicalCores();
  return NumCores;
}

#if defined(__i386__) || defined(_M_IX86) || \
    defined(__x86_64__) || defined(_M_X64)
bool sys::getHostCPUFeatures(StringMap<bool> &Features) {
  unsigned EAX = 0, EBX = 0, ECX = 0, EDX = 0;
  unsigned MaxLevel;

  if (getX86CpuIDAndInfo(0, &MaxLevel, &EBX, &ECX, &EDX) || MaxLevel < 1)
    return false;

  getX86CpuIDAndInfo(1, &EAX, &EBX, &ECX, &EDX);

  Features["cx8"]    = (EDX >>  8) & 1;
  Features["cmov"]   = (EDX >> 15) & 1;
  Features["mmx"]    = (EDX >> 23) & 1;
  Features["fxsr"]   = (EDX >> 24) & 1;
  Features["sse"]    = (EDX >> 25) & 1;
  Features["sse2"]   = (EDX >> 26) & 1;

  Features["sse3"]   = (ECX >>  0) & 1;
  Features["pclmul"] = (ECX >>  1) & 1;
  Features["ssse3"]  = (ECX >>  9) & 1;
  Features["cx16"]   = (ECX >> 13) & 1;
  Features["sse4.1"] = (ECX >> 19) & 1;
  Features["sse4.2"] = (ECX >> 20) & 1;
  Features["movbe"]  = (ECX >> 22) & 1;
  Features["popcnt"] = (ECX >> 23) & 1;
  Features["aes"]    = (ECX >> 25) & 1;
  Features["rdrnd"]  = (ECX >> 30) & 1;

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  bool HasXSave = ((ECX >> 27) & 1) && !getX86XCR0(&EAX, &EDX);
  bool HasAVXSave = HasXSave && ((ECX >> 28) & 1) && ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVXSave && ((EAX & 0xe0) == 0xe0);
#endif
  // AMX requires additional context to be saved by the OS.
  const unsigned AMXBits = (1 << 17) | (1 << 18);
  bool HasAMXSave = HasXSave && ((EAX & AMXBits) == AMXBits);

  Features["avx"]   = HasAVXSave;
  Features["fma"]   = ((ECX >> 12) & 1) && HasAVXSave;
  // Only enable XSAVE if OS has enabled support for saving YMM state.
  Features["xsave"] = ((ECX >> 26) & 1) && HasAVXSave;
  Features["f16c"]  = ((ECX >> 29) & 1) && HasAVXSave;

  unsigned MaxExtLevel;
  getX86CpuIDAndInfo(0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !getX86CpuIDAndInfo(0x80000001, &EAX, &EBX, &ECX, &EDX);
  Features["sahf"]   = HasExtLeaf1 && ((ECX >>  0) & 1);
  Features["lzcnt"]  = HasExtLeaf1 && ((ECX >>  5) & 1);
  Features["sse4a"]  = HasExtLeaf1 && ((ECX >>  6) & 1);
  Features["prfchw"] = HasExtLeaf1 && ((ECX >>  8) & 1);
  Features["xop"]    = HasExtLeaf1 && ((ECX >> 11) & 1) && HasAVXSave;
  Features["lwp"]    = HasExtLeaf1 && ((ECX >> 15) & 1);
  Features["fma4"]   = HasExtLeaf1 && ((ECX >> 16) & 1) && HasAVXSave;
  Features["tbm"]    = HasExtLeaf1 && ((ECX >> 21) & 1);
  Features["mwaitx"] = HasExtLeaf1 && ((ECX >> 29) & 1);

  Features["64bit"]  = HasExtLeaf1 && ((EDX >> 29) & 1);

  // Miscellaneous memory related features, detected by
  // using the 0x80000008 leaf of the CPUID instruction
  bool HasExtLeaf8 = MaxExtLevel >= 0x80000008 &&
                     !getX86CpuIDAndInfo(0x80000008, &EAX, &EBX, &ECX, &EDX);
  Features["clzero"]   = HasExtLeaf8 && ((EBX >> 0) & 1);
  Features["wbnoinvd"] = HasExtLeaf8 && ((EBX >> 9) & 1);

  bool HasLeaf7 =
      MaxLevel >= 7 && !getX86CpuIDAndInfoEx(0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  Features["fsgsbase"]   = HasLeaf7 && ((EBX >>  0) & 1);
  Features["sgx"]        = HasLeaf7 && ((EBX >>  2) & 1);
  Features["bmi"]        = HasLeaf7 && ((EBX >>  3) & 1);
  // AVX2 is only supported if we have the OS save support from AVX.
  Features["avx2"]       = HasLeaf7 && ((EBX >>  5) & 1) && HasAVXSave;
  Features["bmi2"]       = HasLeaf7 && ((EBX >>  8) & 1);
  Features["invpcid"]    = HasLeaf7 && ((EBX >> 10) & 1);
  Features["rtm"]        = HasLeaf7 && ((EBX >> 11) & 1);
  // AVX512 is only supported if the OS supports the context save for it.
  Features["avx512f"]    = HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save;
  Features["avx512dq"]   = HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save;
  Features["rdseed"]     = HasLeaf7 && ((EBX >> 18) & 1);
  Features["adx"]        = HasLeaf7 && ((EBX >> 19) & 1);
  Features["avx512ifma"] = HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save;
  Features["clflushopt"] = HasLeaf7 && ((EBX >> 23) & 1);
  Features["clwb"]       = HasLeaf7 && ((EBX >> 24) & 1);
  Features["avx512pf"]   = HasLeaf7 && ((EBX >> 26) & 1) && HasAVX512Save;
  Features["avx512er"]   = HasLeaf7 && ((EBX >> 27) & 1) && HasAVX512Save;
  Features["avx512cd"]   = HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save;
  Features["sha"]        = HasLeaf7 && ((EBX >> 29) & 1);
  Features["avx512bw"]   = HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save;
  Features["avx512vl"]   = HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save;

  Features["prefetchwt1"]     = HasLeaf7 && ((ECX >>  0) & 1);
  Features["avx512vbmi"]      = HasLeaf7 && ((ECX >>  1) & 1) && HasAVX512Save;
  Features["pku"]             = HasLeaf7 && ((ECX >>  4) & 1);
  Features["waitpkg"]         = HasLeaf7 && ((ECX >>  5) & 1);
  Features["avx512vbmi2"]     = HasLeaf7 && ((ECX >>  6) & 1) && HasAVX512Save;
  Features["shstk"]           = HasLeaf7 && ((ECX >>  7) & 1);
  Features["gfni"]            = HasLeaf7 && ((ECX >>  8) & 1);
  Features["vaes"]            = HasLeaf7 && ((ECX >>  9) & 1) && HasAVXSave;
  Features["vpclmulqdq"]      = HasLeaf7 && ((ECX >> 10) & 1) && HasAVXSave;
  Features["avx512vnni"]      = HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save;
  Features["avx512bitalg"]    = HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save;
  Features["avx512vpopcntdq"] = HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save;
  Features["rdpid"]           = HasLeaf7 && ((ECX >> 22) & 1);
  Features["kl"]              = HasLeaf7 && ((ECX >> 23) & 1); // key locker
  Features["cldemote"]        = HasLeaf7 && ((ECX >> 25) & 1);
  Features["movdiri"]         = HasLeaf7 && ((ECX >> 27) & 1);
  Features["movdir64b"]       = HasLeaf7 && ((ECX >> 28) & 1);
  Features["enqcmd"]          = HasLeaf7 && ((ECX >> 29) & 1);

  Features["uintr"]           = HasLeaf7 && ((EDX >> 5) & 1);
  Features["avx512vp2intersect"] =
      HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save;
  Features["serialize"]       = HasLeaf7 && ((EDX >> 14) & 1);
  Features["tsxldtrk"]        = HasLeaf7 && ((EDX >> 16) & 1);
  // There are two CPUID leafs which information associated with the pconfig
  // instruction:
  // EAX=0x7, ECX=0x0 indicates the availability of the instruction (via the 18th
  // bit of EDX), while the EAX=0x1b leaf returns information on the
  // availability of specific pconfig leafs.
  // The target feature here only refers to the the first of these two.
  // Users might need to check for the availability of specific pconfig
  // leaves using cpuid, since that information is ignored while
  // detecting features using the "-march=native" flag.
  // For more info, see X86 ISA docs.
  Features["pconfig"] = HasLeaf7 && ((EDX >> 18) & 1);
  Features["amx-bf16"]   = HasLeaf7 && ((EDX >> 22) & 1) && HasAMXSave;
  Features["amx-tile"]   = HasLeaf7 && ((EDX >> 24) & 1) && HasAMXSave;
  Features["amx-int8"]   = HasLeaf7 && ((EDX >> 25) & 1) && HasAMXSave;
  bool HasLeaf7Subleaf1 =
      MaxLevel >= 7 && !getX86CpuIDAndInfoEx(0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  Features["avxvnni"]    = HasLeaf7Subleaf1 && ((EAX >> 4) & 1) && HasAVXSave;
  Features["avx512bf16"] = HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save;
  Features["hreset"]     = HasLeaf7Subleaf1 && ((EAX >> 22) & 1);

  bool HasLeafD = MaxLevel >= 0xd &&
                  !getX86CpuIDAndInfoEx(0xd, 0x1, &EAX, &EBX, &ECX, &EDX);

  // Only enable XSAVE if OS has enabled support for saving YMM state.
  Features["xsaveopt"] = HasLeafD && ((EAX >> 0) & 1) && HasAVXSave;
  Features["xsavec"]   = HasLeafD && ((EAX >> 1) & 1) && HasAVXSave;
  Features["xsaves"]   = HasLeafD && ((EAX >> 3) & 1) && HasAVXSave;

  bool HasLeaf14 = MaxLevel >= 0x14 &&
                  !getX86CpuIDAndInfoEx(0x14, 0x0, &EAX, &EBX, &ECX, &EDX);

  Features["ptwrite"] = HasLeaf14 && ((EBX >> 4) & 1);

  bool HasLeaf19 =
      MaxLevel >= 0x19 && !getX86CpuIDAndInfo(0x19, &EAX, &EBX, &ECX, &EDX);
  Features["widekl"] = HasLeaf7 && HasLeaf19 && ((EBX >> 2) & 1);

  return true;
}
#elif defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
bool sys::getHostCPUFeatures(StringMap<bool> &Features) {
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  if (!P)
    return false;

  SmallVector<StringRef, 32> Lines;
  P->getBuffer().split(Lines, "\n");

  SmallVector<StringRef, 32> CPUFeatures;

  // Look for the CPU features.
  for (unsigned I = 0, E = Lines.size(); I != E; ++I)
    if (Lines[I].startswith("Features")) {
      Lines[I].split(CPUFeatures, ' ');
      break;
    }

#if defined(__aarch64__)
  // Keep track of which crypto features we have seen
  enum { CAP_AES = 0x1, CAP_PMULL = 0x2, CAP_SHA1 = 0x4, CAP_SHA2 = 0x8 };
  uint32_t crypto = 0;
#endif

  for (unsigned I = 0, E = CPUFeatures.size(); I != E; ++I) {
    StringRef LLVMFeatureStr = StringSwitch<StringRef>(CPUFeatures[I])
#if defined(__aarch64__)
                                   .Case("asimd", "neon")
                                   .Case("fp", "fp-armv8")
                                   .Case("crc32", "crc")
#else
                                   .Case("half", "fp16")
                                   .Case("neon", "neon")
                                   .Case("vfpv3", "vfp3")
                                   .Case("vfpv3d16", "d16")
                                   .Case("vfpv4", "vfp4")
                                   .Case("idiva", "hwdiv-arm")
                                   .Case("idivt", "hwdiv")
#endif
                                   .Default("");

#if defined(__aarch64__)
    // We need to check crypto separately since we need all of the crypto
    // extensions to enable the subtarget feature
    if (CPUFeatures[I] == "aes")
      crypto |= CAP_AES;
    else if (CPUFeatures[I] == "pmull")
      crypto |= CAP_PMULL;
    else if (CPUFeatures[I] == "sha1")
      crypto |= CAP_SHA1;
    else if (CPUFeatures[I] == "sha2")
      crypto |= CAP_SHA2;
#endif

    if (LLVMFeatureStr != "")
      Features[LLVMFeatureStr] = true;
  }

#if defined(__aarch64__)
  // If we have all crypto bits we can add the feature
  if (crypto == (CAP_AES | CAP_PMULL | CAP_SHA1 | CAP_SHA2))
    Features["crypto"] = true;
#endif

  return true;
}
#elif defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
bool sys::getHostCPUFeatures(StringMap<bool> &Features) {
  if (IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE))
    Features["neon"] = true;
  if (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE))
    Features["crc"] = true;
  if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE))
    Features["crypto"] = true;

  return true;
}
#else
bool sys::getHostCPUFeatures(StringMap<bool> &Features) { return false; }
#endif

std::string sys::getProcessTriple() {
  std::string TargetTripleString = updateTripleOSVersion(LLVM_HOST_TRIPLE);
  Triple PT(Triple::normalize(TargetTripleString));

  if (sizeof(void *) == 8 && PT.isArch32Bit())
    PT = PT.get64BitArchVariant();
  if (sizeof(void *) == 4 && PT.isArch64Bit())
    PT = PT.get32BitArchVariant();

  return PT.str();
}
