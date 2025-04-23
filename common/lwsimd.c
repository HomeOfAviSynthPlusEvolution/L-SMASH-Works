/*****************************************************************************
 * lwsimd.c
 *****************************************************************************
 * Copyright (C) 2013-2015 L-SMASH Works project
 *
 * Authors: rigaya <rigaya34589@live.jp>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include <stdint.h>

#ifdef __GNUC__
static void __cpuid(int CPUInfo[4], int prm)
{
    __asm volatile("cpuid" : "=a"(CPUInfo[0]), "=b"(CPUInfo[1]), "=c"(CPUInfo[2]), "=d"(CPUInfo[3]) : "a"(prm));
    return;
}
#else
#include <intrin.h>
#endif /* __GNUC__ */

static int check_xgetbv(void)
{
#if defined(_MSC_VER) && defined(_XCR_XFEATURE_ENABLED_MASK)
    uint64_t eax = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
#elif defined(__GNUC__)
    uint32_t eax;
    uint32_t edx;
    __asm volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
#else
    uint32_t eax = 0;
#endif
    return (eax & 0x6) == 0x6;
}

int lw_check_sse2()
{
    int CPUInfo[4];
    __cpuid(CPUInfo, 1);
    return (CPUInfo[3] & 0x04000000) != 0;
}

int lw_check_ssse3()
{
    int CPUInfo[4];
    __cpuid(CPUInfo, 1);
    return (CPUInfo[2] & 0x00000200) != 0;
}

int lw_check_sse41()
{
    int CPUInfo[4];
    __cpuid(CPUInfo, 1);
    return (CPUInfo[2] & 0x00080000) != 0;
}

int lw_check_avx2()
{
    int CPUInfo[4];
    __cpuid(CPUInfo, 1);
    if ((CPUInfo[2] & 0x18000000) == 0x18000000 && check_xgetbv()) {
        __cpuid(CPUInfo, 7);
        return (CPUInfo[1] & 0x00000020) != 0;
    }
    return 0;
}
