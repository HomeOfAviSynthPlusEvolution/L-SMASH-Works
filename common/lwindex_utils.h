/*****************************************************************************
 * lwindex_utils.h
 *****************************************************************************/

/* This file is available under an ISC license. */

#ifndef LWINDEX_UTILS_H
#define LWINDEX_UTILS_H

#include "lwindex.h"
#include "osdep.h"

void print_index(FILE* index, const char* format, ...);
uint64_t xxhash_file(const char* file_path, int64_t file_size);
unsigned xxhash32_file(const char* file_path, int64_t file_size);
char* create_lwi_path(lwlibav_option_t* opt);

#endif // !LWINDEX_UTILS_H
