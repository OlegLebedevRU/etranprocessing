/* miniz.h - Single-header zip archive reading & inflate library */
#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char mz_uint8;
typedef unsigned short mz_uint16;
typedef unsigned int mz_uint32;
typedef unsigned long long mz_uint64;
typedef int mz_bool;

#define MZ_FALSE (0)
#define MZ_TRUE (1)

typedef size_t (*mz_file_read_func)(void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n);

typedef enum {
    MZ_ZIP_MODE_INVALID = 0,
    MZ_ZIP_MODE_READING = 1
} mz_zip_mode;

typedef struct {
    mz_uint32 m_file_index;
    mz_uint64 m_central_dir_ofs;
    mz_uint16 m_version_made_by;
    mz_uint16 m_version_needed;
    mz_uint16 m_bit_flag;
    mz_uint16 m_method;
    mz_uint64 m_comp_size;
    mz_uint64 m_uncomp_size;
    mz_uint32 m_crc32;
    char m_filename[512];
    char m_comment[512];
    mz_bool m_is_directory;
    mz_bool m_is_encrypted;
    mz_bool m_is_supported;
} mz_zip_archive_file_stat;

typedef struct {
    mz_uint64 m_archive_size;
    mz_uint64 m_central_directory_file_ofs;
    mz_uint32 m_total_files;
    mz_zip_mode m_zip_mode;
    void *m_pAlloc_opaque;
    mz_file_read_func m_pRead;
    void *m_pIO_opaque;
    void *m_pState;
} mz_zip_archive;

mz_bool mz_zip_reader_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint32 flags);
mz_bool mz_zip_reader_end(mz_zip_archive *pZip);
mz_uint32 mz_zip_reader_get_num_files(mz_zip_archive *pZip);
mz_bool mz_zip_reader_file_stat(mz_zip_archive *pZip, mz_uint32 file_index, mz_zip_archive_file_stat *pStat);
mz_bool mz_zip_reader_is_file_a_directory(mz_zip_archive *pZip, mz_uint32 file_index);
mz_bool mz_zip_reader_extract_to_file(mz_zip_archive *pZip, mz_uint32 file_index, const char *pDst_filename, mz_uint32 flags);
void *mz_zip_reader_extract_file_to_heap(mz_zip_archive *pZip, mz_uint32 file_index, size_t *pSize, mz_uint32 flags);

#ifdef __cplusplus
}
#endif
