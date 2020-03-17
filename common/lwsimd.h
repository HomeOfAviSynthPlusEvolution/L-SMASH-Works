/*****************************************************************************
 * lwsimd.h
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

#ifdef __GNUC__
#define LW_ALIGN(x) __attribute__((aligned(x)))
#define LW_FUNC_ALIGN __attribute__((force_align_arg_pointer))
#define LW_FORCEINLINE inline __attribute__((always_inline))
#else
#define LW_ALIGN(x) __declspec(align(x))
#define LW_FUNC_ALIGN
#define LW_FORCEINLINE __forceinline
#endif

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

int lw_check_sse2();
int lw_check_ssse3();
int lw_check_sse41();
int lw_check_avx2();

#ifdef __cplusplus
}
#endif  /* __cplusplus */
