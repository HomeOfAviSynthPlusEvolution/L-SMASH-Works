/*****************************************************************************
 * lwindex_utils.c
 *****************************************************************************/

/* This file is available under an ISC license. */

#include "lwindex_utils.h"
#include "xxhash.h"

void print_index(FILE* index, const char* format, ...)
{
    if (!index)
        return;
    va_list args;
    va_start(args, format);
    vfprintf(index, format, args);
    va_end(args);
}

/* Hash the first and last mebibytes. */
uint64_t xxhash_file(const char* file_path, int64_t file_size)
{
    FILE* fp = lw_fopen(file_path, "rb");
    if (!fp)
        return 0;
    uint8_t* file_buffer = (uint8_t*)lw_malloc_zero(1 << 21);
    const size_t read_len = 1 << 20;
    size_t buffer_len = fread(file_buffer, 1, read_len, fp);
    if (file_size > (1 << 21)) {
        /* Only if file is larger than 2 mebibytes */
        fseek(fp, -(1 << 20), SEEK_END);
        buffer_len += fread(file_buffer + buffer_len, 1, read_len, fp);
    }
    fclose(fp);
    uint64_t hash = XXH3_64bits(file_buffer, buffer_len);
    lw_free(file_buffer);
    return hash;
}

/* Hash the first and last mebibytes. */
unsigned xxhash32_file(const char* file_path, int64_t file_size)
{
    uint8_t* file_buffer = (uint8_t*)lw_malloc_zero(1 << 21);
    const size_t read_len = 1 << 20;
    FILE* fp = lw_fopen(file_path, "rb");
    size_t buffer_len = fread(file_buffer, 1, read_len, fp);
    if (file_size > (1 << 21)) {
        /* Only if file is larger than 2 mebibytes */
        fseek(fp, -(1 << 20), SEEK_END);
        buffer_len += fread(file_buffer + buffer_len, 1, read_len, fp);
    }
    fclose(fp);
    unsigned hash = XXH32(file_buffer, buffer_len, 0);
    lw_free(file_buffer);
    return hash;
}

char* create_lwi_path(lwlibav_option_t* opt)
{
    if (!opt->cache_dir || opt->cache_dir[0] == '\0') {
        char* buf = lw_malloc_zero(strlen(opt->file_path) + 5);
        sprintf(buf, "%s.lwi", opt->file_path);
        return buf;
    }

    const int max_filename = 254; // be conservative
    const char* dir = opt->cache_dir ? opt->cache_dir : ".";
    const char* rpath = lw_realpath(opt->file_path, NULL);
    char* malloced = NULL;
    if (rpath)
        malloced = (char*)rpath;
    else // realpath on Unix might fail if the file does not exist.
        rpath = opt->file_path;
    const char* suffix = ".lwi";
    int l = strlen(rpath);
    const int max_elem_size = max_filename - strlen(suffix);

    // shorten path from the front until it fits into max_filename UTF-8 bytes.
    const char* p = rpath;
    while (l > max_elem_size && *p != '\0') {
        if ((*p & 0x80) == 0)
            l--, p++;
        else if ((*p & 0xe0) == 0xc0)
            l -= 2, p += 2;
        else if ((*p & 0xe0) == 0xe0)
            l -= 3, p += 3;
        else if ((*p & 0xf8) == 0xf0)
            l -= 4, p += 4;
        assert(l >= 0);
    }

    char* buf = (char*)lw_malloc_zero(strlen(dir) + 1 + max_filename + 1);
    char* q = strcpy(buf, dir) + strlen(dir);
    *q++ = '/';
    for (; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':')
            *q++ = '_';
        else
            *q++ = *p;
    }
    strcpy(q, suffix);
    lw_free(malloced);
    return buf;
}
