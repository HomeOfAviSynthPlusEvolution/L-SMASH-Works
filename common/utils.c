/*****************************************************************************
 * utils.c / utils.cpp
 *****************************************************************************
 * Copyright (C) 2012-2015 L-SMASH Works project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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

#include "cpp_compat.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

void* lw_malloc_zero(size_t size)
{
    void* p = malloc(size);
    if (!p)
        return NULL;
    memset(p, 0, size);
    return p;
}

void lw_free(void* pointer)
{
    free(pointer);
}

void lw_freep(void* pointer)
{
    if (!pointer)
        return;
    void** p = (void**)pointer;
    free(*p);
    *p = NULL;
}

void* lw_memdup(void* src, size_t size)
{
    if (size == 0)
        return NULL;
    void* dst = malloc(size);
    if (!dst)
        return NULL;
    memcpy(dst, src, size);
    return dst;
}

static void lw_log_write_message(lw_log_handler_t* lhp, lw_log_level level, char* message, const char* format, va_list args)
{
    const char* prefix;
    switch (level) {
    case LW_LOG_FATAL:
        prefix = "Fatal";
        break;
    case LW_LOG_ERROR:
        prefix = "Error";
        break;
    case LW_LOG_WARNING:
        prefix = "Warning";
        break;
    case LW_LOG_INFO:
        prefix = "Info";
        break;
    default:
        prefix = "Unknown";
        break;
    }
    char temp[512];
    vsprintf(temp, format, args);
    if (lhp->name)
        sprintf(message, "%s [%s]: %s", lhp->name, prefix, temp);
    else
        sprintf(message, "[%s]: %s", prefix, temp);
}

void lw_log_show(lw_log_handler_t* lhp, lw_log_level level, const char* format, ...)
{
    if (!lhp || !lhp->priv || !lhp->show_log || level < lhp->level)
        return;
    va_list args;
    va_start(args, format);
    char message[1024];
    lw_log_write_message(lhp, level, message, format, args);
    va_end(args);
    lhp->show_log(lhp, level, message);
}

int lw_check_file_extension(const char* file_name, const char* extension)
{
    int extension_length = strlen(extension);
    int file_name_length = strlen(file_name);
    if (file_name_length - extension_length <= 1)
        return -1;
    const char* ext = &file_name[file_name_length - extension_length];
    return ext[-1] != '.' || memcmp(extension, ext, extension_length) ? -1 : 0;
}

static inline double lw_round(double x)
{
    return x > 0 ? floor(x + 0.5) : ceil(x - 0.5);
}

static inline double sigexp10(double value, double* exponent)
{
    /* This function separates significand and exp10 from double floating point. */
    *exponent = 1;
    while (value < 1) {
        value *= 10;
        *exponent /= 10;
    }
    while (value >= 10) {
        value /= 10;
        *exponent *= 10;
    }
    return value;
}

int lw_try_rational_framerate(double framerate, int64_t* framerate_num, int64_t* framerate_den, uint64_t timebase)
{
#define DOUBLE_EPSILON 5e-5
    if (framerate == 0)
        return 0;
    uint64_t fps_den[3] = { 0 };
    uint64_t fps_num[3] = { 0 };
    double error[3] = { DOUBLE_EPSILON, DOUBLE_EPSILON, DOUBLE_EPSILON };
    double exponent;
    double fps_sig = sigexp10(framerate, &exponent);
    uint64_t base[3] = { timebase, 1001, 1 };
    for (int i = 0; i < 3; i++) {
        if (i && base[i] == base[0])
            continue;
        if (i == 2 && (error[0] < DOUBLE_EPSILON || error[1] < DOUBLE_EPSILON))
            break;
        for (int j = 1;; j++) {
            fps_den[i] = j * base[i];
            fps_num[i] = (uint64_t)(lw_round(fps_den[i] * fps_sig) * exponent);
            if (fps_num[i] > INT32_MAX) {
                error[i] = DOUBLE_EPSILON;
                break;
            }
            error[i] = fabs(((double)fps_num[i] / fps_den[i]) / exponent - fps_sig);
            if (error[i] < DOUBLE_EPSILON)
                break;
        }
    }
    double min_error = DOUBLE_EPSILON;
    for (int i = 0; i < 3; i++)
        if (min_error > error[i]) {
            min_error = error[i];
            reduce_fraction(&fps_num[i], &fps_den[i]);
            *framerate_num = (int64_t)fps_num[i];
            *framerate_den = (int64_t)fps_den[i];
        }
    return (min_error < DOUBLE_EPSILON);
#undef DOUBLE_EPSILON
}

const char** lw_tokenize_string(char* str, /* null-terminated string: separator charactors will be replaced with '\0'. */
    char separator, /* separator */
    char** bufs /* If NULL, allocate memory block internally, which can be deallocated by lw_freep(). */
)
{
    if (!str)
        return NULL;
    char** tokens = bufs ? bufs : (char**)malloc(2 * sizeof(char*));
    if (!tokens)
        return NULL;
    size_t i = 1;
    tokens[0] = str;
    tokens[1] = NULL; /* null-terminated */
    for (char* p = str; *p != '\0'; p++)
        if (*p == separator) {
            *p = '\0';
            if (*(p + 1) != '\0') {
                if (!bufs) {
                    char** tmp = (char**)realloc(tokens, (i + 2) * sizeof(char*));
                    if (!tmp)
                        break;
                    tokens = tmp;
                }
                tokens[i] = p + 1;
                tokens[++i] = NULL; /* null-terminated */
            }
        }
    return (const char**)tokens;
}
