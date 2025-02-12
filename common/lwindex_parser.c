/*****************************************************************************
 * lwindex_parser.c
 *****************************************************************************
 * Copyright (C) 2012-2025 L-SMASH Works project
 *
 * Authors: Xinyue Lu <i@7086.in>
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

#include "lwindex_parser.h"
#include "lwindex_sscanf_unrolled.h"

#define BUFFER_SIZE (1<<14) // 16KB

typedef struct {
    char *buffer;
    size_t size;
    size_t current_pos;
    FILE *file;
    int64_t file_offset_of_buffer_start;
} BufferedFile;

static BufferedFile global_buffered_file = { NULL, 0, 0, NULL };

static int stream_mapping[MAX_STREAM_ID];

static char *buffered_fgets(char *str, int n, FILE *stream) {
    if (str == NULL || n <= 0 || stream == NULL) {
        return NULL;
    }

    if (global_buffered_file.file != stream) {
        // New file, initialize/reset the buffer
        if (global_buffered_file.buffer != NULL) {
            free(global_buffered_file.buffer);
        }
        global_buffered_file.buffer = NULL;
        global_buffered_file.size = 0;
        global_buffered_file.current_pos = 0;
        global_buffered_file.file = stream;
        global_buffered_file.file_offset_of_buffer_start = ftell(stream);
        if (global_buffered_file.file_offset_of_buffer_start == -1L) {
            global_buffered_file.file_offset_of_buffer_start = 0;
        }
    }

    int i = 0;
    int64_t current_read_start_offset = -1;
    while (i < n - 1) {  // Leave space for null terminator
        if (global_buffered_file.current_pos >= global_buffered_file.size) {
            // Buffer empty, need to read more data
            if (global_buffered_file.buffer == NULL) {
                global_buffered_file.buffer = (char *)malloc(BUFFER_SIZE);
                if (global_buffered_file.buffer == NULL) {
                    perror("malloc failed");
                    return NULL; // Or handle the error differently
                }
            }

            current_read_start_offset = ftell(stream);
            if (current_read_start_offset == -1L) {
                current_read_start_offset = global_buffered_file.file_offset_of_buffer_start + global_buffered_file.current_pos;
            }
            global_buffered_file.file_offset_of_buffer_start = current_read_start_offset;

            global_buffered_file.size = fread(global_buffered_file.buffer, 1, BUFFER_SIZE, stream);
            global_buffered_file.current_pos = 0;

            if (global_buffered_file.size == 0) {
                // End of file or error
                if (i == 0) {
                    // No characters read before EOF
                    return NULL;
                }
                else {
                    // Some characters read before EOF, terminate and return
                    str[i] = '\0';
                    return str;
                }
            }
        }


        str[i] = global_buffered_file.buffer[global_buffered_file.current_pos];
        global_buffered_file.current_pos++;
        i++;

        if (str[i - 1] == '\n') {
            // Line end found
            str[i] = '\0';
            return str;
        }
    }

    // Buffer full, no newline encountered
    str[i] = '\0';  // Null-terminate
    return str;
}

static size_t buffered_fread(void *ptr, size_t length, FILE *stream) {
    if (ptr == NULL || stream == NULL || length == 0) {
        return 0;
    }

    if (global_buffered_file.file != stream) {
        // New file, initialize/reset the buffer
        if (global_buffered_file.buffer != NULL) {
            free(global_buffered_file.buffer);
        }
        global_buffered_file.buffer = NULL;
        global_buffered_file.size = 0;
        global_buffered_file.current_pos = 0;
        global_buffered_file.file = stream;
        global_buffered_file.file_offset_of_buffer_start = ftell(stream);
        if (global_buffered_file.file_offset_of_buffer_start == -1L) {
            global_buffered_file.file_offset_of_buffer_start = 0;
        }
    }

    size_t bytes_read = 0;
    char *dest = (char *)ptr;
    int64_t current_read_start_offset = -1;

    while (bytes_read < length) {
        if (global_buffered_file.current_pos >= global_buffered_file.size) {
            // Buffer empty, need to read more data
            if (global_buffered_file.buffer == NULL) {
                global_buffered_file.buffer = (char *)malloc(BUFFER_SIZE);
                if (global_buffered_file.buffer == NULL) {
                    perror("malloc failed");
                    return bytes_read; // Or handle the error differently
                }
            }

            current_read_start_offset = ftell(stream);
            if (current_read_start_offset == -1L) {
                current_read_start_offset = global_buffered_file.file_offset_of_buffer_start + global_buffered_file.current_pos;
            }
            global_buffered_file.file_offset_of_buffer_start = current_read_start_offset;

            global_buffered_file.size = fread(global_buffered_file.buffer, 1, BUFFER_SIZE, stream);
            global_buffered_file.current_pos = 0;

            if (global_buffered_file.size == 0) {
                // End of file or error
                return bytes_read;
            }
        }

        size_t remaining_in_buffer = global_buffered_file.size - global_buffered_file.current_pos;
        size_t bytes_to_copy = (length - bytes_read < remaining_in_buffer) ? (length - bytes_read) : remaining_in_buffer;

        memcpy(dest + bytes_read, global_buffered_file.buffer + global_buffered_file.current_pos, bytes_to_copy);

        global_buffered_file.current_pos += bytes_to_copy;
        bytes_read += bytes_to_copy;
    }

    return bytes_read;
}

static void buffer_clear() {
    if (global_buffered_file.buffer != NULL) {
        free(global_buffered_file.buffer);
    }
    global_buffered_file.buffer = NULL;
    global_buffered_file.size = 0;
    global_buffered_file.current_pos = 0;
    global_buffered_file.file = NULL;
    global_buffered_file.file_offset_of_buffer_start = -1;
}


static int read_tag(char *buffer, char *tag, char *attribute, char *content) {
    // Find start and end of the tag

    char *tag_start = strchr(buffer, '<');

    char *tag_end = strchr(buffer, '>');
    if (!tag_start || !tag_end) {
        return 0; // Invalid tag format
    }

    // Extract the tag name
    size_t tag_length = tag_end - tag_start - 1;
    if (tag_length >= MAX_TAG_LENGTH) {
        return 0; // Tag name too long
    }
    strncpy(tag, tag_start + 1, tag_length);
    tag[tag_length] = '\0';

    // Check for <tag=attribute> format
    char *equals_sign = strchr(buffer, '=');
    if (equals_sign != NULL && equals_sign < tag_end) {
        // Extract data after the '=' sign
        char *data_start = equals_sign + 1;
        size_t data_length = tag_end - data_start;
        if (data_length >= MAX_VALUE_LENGTH) {
            return 0; // Data too long
        }
        strncpy(attribute, data_start, data_length);
        attribute[data_length] = '\0';
        tag_length = equals_sign - tag_start - 1;
        tag[tag_length] = '\0';
    }
    // Extract the content between the tags, if any.  <tag>data</tag> format
    char *content_start = tag_end + 1;
    char *content_end = strstr(content_start, "</");

    if (content_end == NULL) {
        content[0] = '\0';
        return 1;
    }

    //check tag name
    if (strncmp(content_end + 2, tag, strlen(tag)) != 0 || content_end[strlen(tag) + 2] != '>') {
        content[0] = '\0';
        return 1;
    }

    size_t content_length = content_end - content_start;
    if (content_length >= MAX_VALUE_LENGTH) {
        return 0; // Content too long
    }

    strncpy(content, content_start, content_length);
    content[content_length] = '\0';

    return 1;
}

static int parse_stream_info(char *content, char *line, stream_info_entry_t *stream_info) {
    uint32_t stream_index;
    uint32_t codec_type;
    if (sscanf(content, "%" SCNu32 ",%" SCNu32,
        &stream_index,
        &codec_type) != 2) {
        return 0;
    }
    stream_info->stream_index = stream_index;
    stream_info->codec_type = codec_type;

    if (stream_info->codec_type == AV_STREAM_TYPE_VIDEO) {
        // Codec=173,TimeBase=1/90000,Width=3840,Height=2160,Format=yuv420p10le,ColorSpace=9
        if (sscanf(line, "Codec=%d,TimeBase=%d/%d,Width=%d,Height=%d,Format=%[^,],ColorSpace=%d",
            &stream_info->codec,
            &stream_info->time_base.num, &stream_info->time_base.den,
            &stream_info->data.type0.width, &stream_info->data.type0.height,
            stream_info->format,
            &stream_info->data.type0.color_space) == 7) {
            return 1;
        }
    }
    else if (stream_info->codec_type == AV_STREAM_TYPE_AUDIO) {
        // Codec=86019,TimeBase=1/90000,Channels=6:0x60f,Rate=48000,Format=fltp,BPS=32
        if (sscanf(line, "Codec=%d,TimeBase=%d/%d,Channels=%d:0x%" SCNx64 ",Rate=%d,Format=%[^,],BPS=%d",
            &stream_info->codec,
            &stream_info->time_base.num, &stream_info->time_base.den,
            &stream_info->data.type1.channels,
            &stream_info->data.type1.layout,
            &stream_info->data.type1.sample_rate,
            stream_info->format,
            &stream_info->bits_per_sample) == 8) {
            return 1;
        }
    }

    return 0;
}

static int parse_extra_data_entry(char *line, int codec_type, extra_data_entry_t *extra_data) {
    if (codec_type == AV_STREAM_TYPE_VIDEO) {
        // Size=104,Codec=173,4CC=0x564d4448,Width=3840,Height=2160,Format=yuv420p10le,BPS=0
        if (sscanf(line, "Size=%" SCNu32 ",Codec=%" SCNu32 ",4CC=0x%" SCNx32 ",Width=%" SCNd32 ",Height=%" SCNd32 ",Format=%[^,],BPS=%" SCNd32,
            &extra_data->size, &extra_data->codec, &extra_data->fourcc,
            &extra_data->data.type0.width, &extra_data->data.type0.height, extra_data->format,
            &extra_data->bits_per_sample) != 7) {
            return 0;
        }
    }
    else if (codec_type == AV_STREAM_TYPE_AUDIO) {
        // Size=0,Codec=86060,4CC=0x332d4341,Layout=0x63f,Rate=48000,Format=s32,BPS=24,Align=0
        if (sscanf(line, "Size=%" SCNu32 ",Codec=%" SCNu32 ",4CC=0x%" SCNx32 ",Layout=0x%" SCNx64 ",Rate=%" SCNd32 ",Format=%[^,],BPS=%" SCNd32 ",Align=%" SCNd32,
            &extra_data->size, &extra_data->codec, &extra_data->fourcc,
            &extra_data->data.type1.layout, &extra_data->data.type1.sample_rate, extra_data->format,
            &extra_data->bits_per_sample, &extra_data->data.type1.block_align) != 8) {
            return 0;
        }
    }

    return 1;
}

static size_t calculate_new_size(size_t current_size) {
    // Expand the size by 1/2 if size is below 2 million records, otherwise expand by 1 million records
    // This is to avoid allocating too much memory for the index entries when the index file is large
    if (current_size >= (1 << 21)) {
        return current_size + (1 << 20);
    }
    size_t power_of_2 = current_size & (current_size - 1);
    if (power_of_2 == 0) {
        return current_size + (current_size >> 1);
    }
    else {
        return power_of_2 << 1;
    }
}

lwindex_data_t *lwindex_parse(FILE *index, int include_video, int include_audio) {
    if (!index) {
        return NULL;
    }

    lwindex_data_t *data = (lwindex_data_t *)malloc(sizeof(lwindex_data_t));
    if (!data) {
        fprintf(stderr, "Failed to allocate memory for lwindex_data_t");
        return NULL;
    }
    memset(data, 0, sizeof(lwindex_data_t));

    data->active_video_stream_index_pos = -1;
    data->active_audio_stream_index_pos = -1;

    char *tag = (char *)malloc(MAX_TAG_LENGTH);
    char *attribute = (char *)malloc(MAX_VALUE_LENGTH);
    char *content = (char *)malloc(MAX_VALUE_LENGTH);
    char *line = (char *)malloc(MAX_LINE_LENGTH);
    char *next_line = (char *)malloc(MAX_LINE_LENGTH);
    if (!tag || !attribute || !content || !line || !next_line)

    {
        fprintf(stderr, "Failed to allocate memory for internal variables");
        goto fail_parsing;
    }

    memset(stream_mapping, -1, MAX_STREAM_ID * sizeof(int));

    size_t index_entries_size = INIT_INDEX_ENTRIES;

    // Initial allocation for index entries.  Reallocate later if needed.
    data->index_entries = (index_entry_t *)malloc(INIT_INDEX_ENTRIES * sizeof(index_entry_t));
    if (!data->index_entries) {
        fprintf(stderr, "Failed to allocate memory for index_entries");
        goto fail_parsing;
    }
    memset(data->index_entries, 0, INIT_INDEX_ENTRIES * sizeof(index_entry_t));
    data->num_index_entries = 0;

    data->extra_data_list = (extra_data_list_t *)malloc(MAX_EXTRA_DATA_LIST * sizeof(extra_data_list_t));
    if (!data->extra_data_list) {
        fprintf(stderr, "Failed to allocate memory for extra_data_list");
        goto fail_parsing;
    }
    memset(data->extra_data_list, 0, MAX_EXTRA_DATA_LIST * sizeof(extra_data_list_t));
    data->num_extra_data_list = 0;

    buffer_clear();
    global_buffered_file.file = index;
    global_buffered_file.file_offset_of_buffer_start = ftell(index);
    if (global_buffered_file.file_offset_of_buffer_start == -1L) {
        global_buffered_file.file_offset_of_buffer_start = 0;
    }

    enum index_entry_scope scope = INDEX_ENTRY_SCOPE_GLOBAL;
    while (buffered_fgets(line, MAX_LINE_LENGTH, index) != NULL) {
        if (strncmp(line, "</LibavReaderIndex>", strlen("</LibavReaderIndex>")) == 0) {
            // End of frame scope
            scope = INDEX_ENTRY_SCOPE_GLOBAL;
        }
        else if (strncmp(line, "</LibavReaderIndexFile>", strlen("</LibavReaderIndexFile>")) == 0) {
            // End of global scope
            scope = INDEX_ENTRY_SCOPE_INVALID;
        }
        else if (line[0] == '<') {
            int64_t current_line_start_offset = -1;
            if (global_buffered_file.file_offset_of_buffer_start != -1) {
                current_line_start_offset = global_buffered_file.file_offset_of_buffer_start
                    + global_buffered_file.current_pos - strlen(line);
            }

            read_tag(line, tag, attribute, content);
            if (strcmp(tag, "LSMASHWorksIndexVersion") == 0) {
                strncpy(data->lsmash_works_index_version, attribute, sizeof(data->lsmash_works_index_version) - 1);
                data->lsmash_works_index_version[sizeof(data->lsmash_works_index_version) - 1] = '\0';
            }
            else if (strcmp(tag, "LibavReaderIndexFile") == 0) {
                data->libav_reader_index_file = atoi(attribute);
                scope = INDEX_ENTRY_SCOPE_GLOBAL;
            }
            else if (strcmp(tag, "InputFilePath") == 0) {
                strncpy(data->input_file_path, content, sizeof(data->input_file_path) - 1);
                data->input_file_path[sizeof(data->input_file_path) - 1] = '\0';
            }
            else if (strcmp(tag, "FileSize") == 0) {
                data->file_size = strtoull(attribute, NULL, 10);
            }
            else if (strcmp(tag, "FileLastModificationTime") == 0) {
                data->file_last_modification_time = strtoll(attribute, NULL, 10);
            }
            else if (strcmp(tag, "FileHash") == 0) {
                data->file_hash = strtoull(attribute, NULL, 16);
            }
            else if (strcmp(tag, "LibavReaderIndex") == 0)

            {
                if (sscanf(attribute, "0x%x,%d,%[^>]", (unsigned int *)&data->format_flags, &data->raw_demuxer, data->format_name) != 3) {
                    fprintf(stderr, "Failed to parse libav reader index.\n");
                    goto fail_parsing;
                }
                // Start of frame scope
                scope = INDEX_ENTRY_SCOPE_STREAM;
            }
            else if (strcmp(tag, "ActiveVideoStreamIndex") == 0) {
                data->active_video_stream_index = strtol(content, NULL, 10);
                if (current_line_start_offset != -1) {
                    char* tag_start_in_line = strstr(line, "<ActiveVideoStreamIndex>");
                    data->active_video_stream_index_pos = (tag_start_in_line) ? current_line_start_offset + (tag_start_in_line - line)
                        + strlen("<ActiveVideoStreamIndex>") : -1;
                }
                else {
                    data->active_video_stream_index_pos = -1;
                }
            }
            else if (strcmp(tag, "ActiveAudioStreamIndex") == 0) {
                data->active_audio_stream_index = strtol(content, NULL, 10);
                if (current_line_start_offset != -1) {
                    char* tag_start_in_line = strstr(line, "<ActiveAudioStreamIndex>");
                    data->active_audio_stream_index_pos = (tag_start_in_line) ? current_line_start_offset + (tag_start_in_line - line)
                        + strlen("<ActiveAudioStreamIndex>") : -1;
                }
                else {
                    data->active_audio_stream_index_pos = -1;
                }
            }
            else if (strcmp(tag, "DefaultAudioStreamIndex") == 0) {
                data->default_audio_stream_index = strtol(content, NULL, 10);
            }
            else if (strcmp(tag, "FillAudioGaps") == 0) {
                data->fill_audio_gaps = strtol(content, NULL, 10);
            }

            else if (strcmp(tag, "StreamInfo") == 0) {
                if (buffered_fgets(line, MAX_LINE_LENGTH, index) == NULL) {
                    fprintf(stderr, "Unexpected end of file while reading stream info.\n");
                    goto fail_parsing;
                }
                stream_info_entry_t *tmp = (stream_info_entry_t *)realloc(data->stream_info, (data->num_streams + 1) * sizeof(stream_info_entry_t));
                if (!tmp) {
                    fprintf(stderr, "Failed to reallocate stream info.\n");
                    goto fail_parsing;
                }
                data->stream_info = tmp;
                data->stream_info[data->num_streams].stream_index_entries = NULL;

                if (!parse_stream_info(attribute, line, &data->stream_info[data->num_streams])) {
                    fprintf(stderr, "Failed to parse stream info.\n");
                    goto fail_parsing;
                }
                stream_mapping[data->stream_info[data->num_streams].stream_index] = data->num_streams;

                if (buffered_fgets(line, MAX_LINE_LENGTH, index) == NULL) {
                    fprintf(stderr, "Unexpected end of file while reading stream info.\n");
                    goto fail_parsing;
                }

                if (strncmp(line, "</StreamInfo>", strlen("</StreamInfo>")) != 0) {
                    fprintf(stderr, "Unexpected tag while reading stream info.\n");
                    goto fail_parsing;
                }

                data->num_streams++;
            }
            else if (strcmp(tag, "VideoConsistentFieldRepeatPict") == 0) {
                data->consistent_field_and_repeat = strtol(content, NULL, 10);
            }
            else if (strcmp(tag, "StreamDuration") == 0) {
                uint8_t stream_index;
                uint8_t codec_type;
                int64_t stream_duration;
                if (sscanf(attribute, "%" SCNu8 ",%" SCNu8,
                    &stream_index,
                    &codec_type) != 2) {
                    fprintf(stderr, "Failed to parse stream duration attribute.\n");
                    goto fail_parsing;
                }

                stream_duration = strtoll(content, NULL, 10);

                stream_info_entry_t *stream = &data->stream_info[stream_index];
                if (stream == NULL) {
                    fprintf(stderr, "Stream index %d not found.\n", stream_index);
                    goto fail_parsing;
                }

                stream->stream_duration = stream_duration;
            }

            else if (strcmp(tag, "StreamIndexEntries") == 0) {

                uint8_t stream_index;
                uint8_t codec_type;
                uint32_t index_entries_count;

                if (sscanf(attribute, "%" SCNu8 ",%" SCNu8 ",%" SCNu32, &stream_index, &codec_type, &index_entries_count) != 3) {
                    fprintf(stderr, "Failed to parse stream index entries attribute.\n");
                    goto fail_parsing;
                }

                stream_info_entry_t *stream = &data->stream_info[stream_index];
                if (stream == NULL) {
                    fprintf(stderr, "Stream index %d not found.\n", stream_index);
                    goto fail_parsing;
                }

                stream_index_entry_t *tmp = (stream_index_entry_t *)malloc(index_entries_count * sizeof(stream_index_entry_t));
                if (!tmp) {
                    fprintf(stderr, "Failed to allocate memory for stream index entries.\n");
                    goto fail_parsing;
                }

                stream->num_stream_index_entries = index_entries_count;
                stream->stream_index_entries = tmp;

                for (unsigned int i = 0; i < index_entries_count; i++) {
                    if (buffered_fgets(line, MAX_LINE_LENGTH, index) == NULL) {
                        fprintf(stderr, "Unexpected end of file while reading stream index entries.\n");
                        goto fail_parsing;
                    }

                    int64_t pos, ts;
                    uint32_t flags, size, distance;
                    if (sscanf_unrolled_stream_index_entry(line, &pos, &ts, &flags, &size, &distance) != 5) {
                        fprintf(stderr, "Failed to parse stream index entry.\n");
                        goto fail_parsing;
                    }
                    stream->stream_index_entries[i].pos = pos;
                    stream->stream_index_entries[i].ts = ts;
                    stream->stream_index_entries[i].flags = flags;
                    stream->stream_index_entries[i].size = size;
                    stream->stream_index_entries[i].distance = distance;
                }
                if (buffered_fgets(line, MAX_LINE_LENGTH, index) == NULL) {
                    fprintf(stderr, "Unexpected end of file while reading stream index entries.\n");
                    goto fail_parsing;
                }
                if (strncmp(line, "</StreamIndexEntries>", strlen("</StreamIndexEntries>")) != 0) {
                    fprintf(stderr, "Unexpected tag while reading stream index entries.\n");
                    goto fail_parsing;
                }
            }
            else if (strcmp(tag, "ExtraDataList") == 0) {
                if (data->num_extra_data_list >= MAX_EXTRA_DATA_LIST) {
                    fprintf(stderr, "Too many extra data list entries.\n");
                    goto fail_parsing;
                }
                extra_data_list_t *extra_data_entry = &data->extra_data_list[data->num_extra_data_list];
                uint32_t stream_index, codec_type;
                if (sscanf(attribute, "%" SCNu32 ",%" SCNu32 ",%" SCNu32,
                    &stream_index,
                    &codec_type,
                    &extra_data_entry->entry_count
                ) != 3) {
                    extra_data_entry->entry_count = 0;
                    fprintf(stderr, "Failed to parse extra data list.\n");
                    goto fail_parsing;
                }
                extra_data_entry->stream_index = stream_index;
                extra_data_entry->codec_type = codec_type;

                extra_data_entry_t *entries = extra_data_entry->entries = (extra_data_entry_t *)malloc(extra_data_entry->entry_count * sizeof(extra_data_entry_t));
                if (!entries) {
                    fprintf(stderr, "Failed to allocate memory for extra data entries.\n");
                    goto fail_parsing;
                }
                memset(entries, 0, extra_data_entry->entry_count * sizeof(extra_data_entry_t));

                for (int i = 0; i < extra_data_entry->entry_count; i++) {
                    if (buffered_fgets(line, MAX_LINE_LENGTH, index) == NULL) {
                        fprintf(stderr, "Unexpected end of file while reading extra data list.\n");
                        goto fail_parsing;
                    }

                    if (!parse_extra_data_entry(line, extra_data_entry->codec_type, &entries[i])) {
                        fprintf(stderr, "Failed to parse extra data entry.\n");
                        goto fail_parsing;
                    }

                    entries[i].binary_data = (char *)malloc(entries[i].size * sizeof(char));
                    if (!entries[i].binary_data) {
                        fprintf(stderr, "Failed to allocate memory for extra data entry binary data.\n");
                        goto fail_parsing;
                    }
                    buffered_fread(entries[i].binary_data, entries[i].size, index);

                    // skip to the end of the line
                    buffered_fgets(line, MAX_LINE_LENGTH, index);
                }

                if (buffered_fgets(line, MAX_LINE_LENGTH, index) == NULL) {
                    fprintf(stderr, "Unexpected end of file while reading extra data list.\n");
                    goto fail_parsing;
                }

                if (strncmp(line, "</ExtraDataList>", strlen("</ExtraDataList>")) != 0) {
                    fprintf(stderr, "Unexpected tag while reading extra data list.\n");
                    goto fail_parsing;
                }

                data->num_extra_data_list++;
            }
            else {
                fprintf(stderr, "Unexpected tag: %s from line %s", tag, line);
            }
        }
        else if (scope == INDEX_ENTRY_SCOPE_STREAM && strncmp(line, "Index=", strlen("Index=")) == 0) {
            if (data->num_index_entries >= index_entries_size && index_entries_size < MAX_INDEX_ENTRIES) {
                index_entries_size = calculate_new_size(index_entries_size);
                index_entry_t *tmp = (index_entry_t *)realloc(data->index_entries, index_entries_size * sizeof(index_entry_t));
                if (!tmp) {
                    fprintf(stderr, "Failed to reallocate index entries.\n");
                    goto fail_parsing;
                }
                data->index_entries = tmp;
            }

            if (buffered_fgets(next_line, MAX_LINE_LENGTH, index) == NULL) {
                fprintf(stderr, "Unexpected end of file while reading index entry.\n");
                goto fail_parsing;
            }

            index_entry_t *index_entry = &data->index_entries[data->num_index_entries];

            int32_t stream_index, extradata_index;
            if (sscanf_unrolled_main_index(line, &stream_index, &index_entry->pos, &index_entry->pts, &index_entry->dts, &extradata_index) != 5) {
                fprintf(stderr, "Failed to parse index entry.\n");
                goto fail_parsing;
            }
            index_entry->stream_index = stream_index;
            index_entry->edi = extradata_index;

            if (stream_mapping[index_entry->stream_index] == -1) {
                fprintf(stderr, "Stream index %d not found.\n", index_entry->stream_index);
                goto fail_parsing;
            }

            if (data->stream_info[stream_mapping[index_entry->stream_index]].codec_type == AV_STREAM_TYPE_VIDEO) {
                if (include_video) {
                    int32_t key, pict_type, poc, repeat_pict, field_info;
                    if (sscanf_unrolled_video_index(next_line, &key, &pict_type, &poc, &repeat_pict, &field_info) != 5) {
                        fprintf(stderr, "Failed to parse video index entry.\n");
                        goto fail_parsing;
                    }
                    data->num_index_entries++;
                    index_entry->data.type0.key = key;
                    index_entry->data.type0.pic = pict_type;
                    index_entry->data.type0.poc = poc;
                    index_entry->data.type0.repeat = repeat_pict;
                    index_entry->data.type0.field = field_info;
                }
            }
            else if (data->stream_info[stream_mapping[index_entry->stream_index]].codec_type == AV_STREAM_TYPE_AUDIO) {
                if (include_audio)
                {
                    int32_t frame_length;
                    if (sscanf_unrolled_audio_index(next_line, &frame_length) != 1) {
                        fprintf(stderr, "Failed to parse audio index entry.\n");
                        goto fail_parsing;
                    }
                    data->num_index_entries++;
                    index_entry->data.type1.length = frame_length;
                }
            }
            else {
                fprintf(stderr, "Unexpected stream type: %d\n", data->stream_info[stream_mapping[index_entry->stream_index]].codec_type);
                goto fail_parsing;
            }

        }

        else {
            fprintf(stderr, "Unexpected content: %s, ignored.\n", line);
        }
    }

    if (scope != INDEX_ENTRY_SCOPE_INVALID) {
        fprintf(stderr, "Unexpected end of file while reading index.\n");
        goto fail_parsing;
    }

    if (tag)       free(tag);
    if (attribute) free(attribute);
    if (content)   free(content);
    if (line)      free(line);
    if (next_line) free(next_line);

    buffer_clear();
    return data;

fail_parsing:
    if (tag)       free(tag);
    if (attribute) free(attribute);
    if (content)   free(content);
    if (line)      free(line);
    if (next_line) free(next_line);

    buffer_clear();
    lwindex_free(data);
    return NULL;
}

void lwindex_free(lwindex_data_t *data) {
    if (!data)
        return;

    if (data->index_entries)
        free(data->index_entries);

    if (data->stream_info) {
        for (int i = 0; i < data->num_streams; i++) {
            if (data->stream_info[i].stream_index_entries)
                free(data->stream_info[i].stream_index_entries);
        }
        free(data->stream_info);
    }

    if (data->extra_data_list) {
        for (int i = 0; i < data->num_extra_data_list; i++) {
            if (data->extra_data_list[i].entries) {
                for (int j = 0; j < data->extra_data_list[i].entry_count; j++) {
                    if (data->extra_data_list[i].entries[j].binary_data)
                        free(data->extra_data_list[i].entries[j].binary_data);
                }
                free(data->extra_data_list[i].entries);
            }
        }
        free(data->extra_data_list);
    }
    free(data);
}
