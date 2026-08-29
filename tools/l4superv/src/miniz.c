/* miniz.c - Deflate / Inflate and ZIP archive reader implementation */
#include "miniz.h"
#include <windows.h>

#define MZ_READ_LE16(p) ((mz_uint16)(((const mz_uint8 *)(p))[0]) | ((mz_uint16)(((const mz_uint8 *)(p))[1]) << 8))
#define MZ_READ_LE32(p) ((mz_uint32)(((const mz_uint8 *)(p))[0]) | ((mz_uint32)(((const mz_uint8 *)(p))[1]) << 8) | ((mz_uint32)(((const mz_uint8 *)(p))[2]) << 16) | ((mz_uint32)(((const mz_uint8 *)(p))[3]) << 24))
#define MZ_READ_LE64(p) ((mz_uint64)MZ_READ_LE32(p) | (((mz_uint64)MZ_READ_LE32((const mz_uint8 *)(p) + 4)) << 32))

/* CRC-32 table and calculation */
static const mz_uint32 s_crc32[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

static mz_uint32 mz_crc32(mz_uint32 crc, const mz_uint8 *ptr, size_t buf_len) {
    crc = ~crc;
    while (buf_len--) {
        mz_uint8 b = *ptr++;
        crc = (crc >> 4) ^ s_crc32[(crc & 0xF) ^ (b & 0xF)];
        crc = (crc >> 4) ^ s_crc32[(crc & 0xF) ^ (b >> 4)];
    }
    return ~crc;
}

/* Tiny inflate implementation */
typedef struct {
    const mz_uint8 *in_ptr;
    const mz_uint8 *in_end;
    mz_uint32 bit_buf;
    int num_bits;
} bit_stream;

static mz_uint32 get_bits(bit_stream *bs, int count) {
    while (bs->num_bits < count) {
        if (bs->in_ptr < bs->in_end) {
            bs->bit_buf |= ((mz_uint32)(*bs->in_ptr++)) << bs->num_bits;
            bs->num_bits += 8;
        } else {
            break;
        }
    }
    mz_uint32 res = bs->bit_buf & ((1U << count) - 1);
    bs->bit_buf >>= count;
    bs->num_bits -= count;
    return res;
}

typedef struct {
    mz_uint16 count[16];
    mz_uint16 symbol[288];
} huff_table;

static void build_huffman_table(huff_table *t, const mz_uint8 *lengths, int num_symbols) {
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < num_symbols; i++) {
        if (lengths[i]) t->count[lengths[i]]++;
    }
    t->count[0] = 0;
    mz_uint16 ofs[16];
    ofs[1] = 0;
    for (int i = 1; i < 15; i++) {
        ofs[i + 1] = ofs[i] + t->count[i];
    }
    for (int i = 0; i < num_symbols; i++) {
        if (lengths[i]) {
            t->symbol[ofs[lengths[i]]++] = (mz_uint16)i;
        }
    }
}

static int decode_huffman(bit_stream *bs, const huff_table *t) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= get_bits(bs, 1);
        int count = t->count[len];
        if (code - count < first) {
            return t->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const mz_uint16 len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const mz_uint8 len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const mz_uint16 dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const mz_uint8 dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const mz_uint8 clen_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

static bool inflate_raw(const mz_uint8 *in_buf, size_t in_len, mz_uint8 *out_buf, size_t out_len) {
    bit_stream bs = { in_buf, in_buf + in_len, 0, 0 };
    size_t out_pos = 0;
    int bfinal = 0;

    while (!bfinal) {
        bfinal = get_bits(&bs, 1);
        int btype = get_bits(&bs, 2);

        if (btype == 0) {
            // Stored block
            bs.bit_buf = 0;
            bs.num_bits = 0;
            if (bs.in_ptr + 4 > bs.in_end) return false;
            mz_uint16 len = MZ_READ_LE16(bs.in_ptr);
            bs.in_ptr += 4;
            if (out_pos + len > out_len || bs.in_ptr + len > bs.in_end) return false;
            memcpy(out_buf + out_pos, bs.in_ptr, len);
            bs.in_ptr += len;
            out_pos += len;
        } else if (btype == 1 || btype == 2) {
            huff_table lt, dt;
            if (btype == 1) {
                // Fixed Huffman
                mz_uint8 ll[288];
                for (int i = 0; i <= 143; i++) ll[i] = 8;
                for (int i = 144; i <= 255; i++) ll[i] = 9;
                for (int i = 256; i <= 279; i++) ll[i] = 7;
                for (int i = 280; i <= 287; i++) ll[i] = 8;
                build_huffman_table(&lt, ll, 288);
                mz_uint8 dl[32];
                for (int i = 0; i < 32; i++) dl[i] = 5;
                build_huffman_table(&dt, dl, 32);
            } else {
                // Dynamic Huffman
                int hlit = get_bits(&bs, 5) + 257;
                int hdist = get_bits(&bs, 5) + 1;
                int hclen = get_bits(&bs, 4) + 4;
                mz_uint8 cl[19] = { 0 };
                for (int i = 0; i < hclen; i++) cl[clen_order[i]] = (mz_uint8)get_bits(&bs, 3);
                huff_table ct;
                build_huffman_table(&ct, cl, 19);

                mz_uint8 lens[288 + 32] = { 0 };
                int num = 0;
                while (num < hlit + hdist) {
                    int sym = decode_huffman(&bs, &ct);
                    if (sym < 16) {
                        lens[num++] = (mz_uint8)sym;
                    } else if (sym == 16) {
                        int rep = get_bits(&bs, 2) + 3;
                        mz_uint8 prev = (num > 0) ? lens[num - 1] : 0;
                        while (rep-- && num < hlit + hdist) lens[num++] = prev;
                    } else if (sym == 17) {
                        int rep = get_bits(&bs, 3) + 3;
                        while (rep-- && num < hlit + hdist) lens[num++] = 0;
                    } else if (sym == 18) {
                        int rep = get_bits(&bs, 7) + 11;
                        while (rep-- && num < hlit + hdist) lens[num++] = 0;
                    } else {
                        return false;
                    }
                }
                build_huffman_table(&lt, lens, hlit);
                build_huffman_table(&dt, lens + hlit, hdist);
            }

            while (1) {
                int sym = decode_huffman(&bs, &lt);
                if (sym < 256) {
                    if (sym < 0 || out_pos >= out_len) return false;
                    out_buf[out_pos++] = (mz_uint8)sym;
                } else if (sym == 256) {
                    break; // End of block
                } else if (sym <= 285) {
                    sym -= 257;
                    int len = len_base[sym] + get_bits(&bs, len_extra[sym]);
                    int dsym = decode_huffman(&bs, &dt);
                    if (dsym < 0 || dsym >= 30) return false;
                    int dist = dist_base[dsym] + get_bits(&bs, dist_extra[dsym]);
                    if ((size_t)dist > out_pos || out_pos + len > out_len) return false;
                    for (int k = 0; k < len; k++) {
                        out_buf[out_pos] = out_buf[out_pos - dist];
                        out_pos++;
                    }
                } else {
                    return false;
                }
            }
        } else {
            return false;
        }
    }
    return (out_pos == out_len);
}

/* ZIP Reader Implementation */
typedef struct {
    mz_uint64 local_header_ofs;
    mz_uint16 method;
    mz_uint32 comp_size;
    mz_uint32 uncomp_size;
    mz_uint32 crc32;
    char filename[MAX_PATH];
    mz_bool is_dir;
} zip_entry_internal;

typedef struct {
    FILE *f;
    mz_uint32 num_entries;
    zip_entry_internal *entries;
} zip_internal_state;

mz_bool mz_zip_reader_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint32 flags) {
    (void)flags;
    if (!pZip || !pFilename) return MZ_FALSE;
    memset(pZip, 0, sizeof(*pZip));

    FILE *f = NULL;
    if (fopen_s(&f, pFilename, "rb") != 0 || !f) {
        return MZ_FALSE;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 22) {
        fclose(f);
        return MZ_FALSE;
    }

    // Find EOCD signature (0x06054b50) in last 64KB
    long search_len = (sz < 65557) ? sz : 65557;
    mz_uint8 *search_buf = (mz_uint8 *)malloc(search_len);
    if (!search_buf) {
        fclose(f);
        return MZ_FALSE;
    }

    fseek(f, sz - search_len, SEEK_SET);
    fread(search_buf, 1, search_len, f);

    long eocd_pos = -1;
    for (long i = search_len - 22; i >= 0; i--) {
        if (MZ_READ_LE32(search_buf + i) == 0x06054b50) {
            eocd_pos = (sz - search_len) + i;
            break;
        }
    }
    free(search_buf);

    if (eocd_pos < 0) {
        fclose(f);
        return MZ_FALSE;
    }

    fseek(f, eocd_pos, SEEK_SET);
    mz_uint8 eocd[22];
    if (fread(eocd, 1, 22, f) != 22) {
        fclose(f);
        return MZ_FALSE;
    }

    mz_uint16 total_entries = MZ_READ_LE16(eocd + 10);
    (void)MZ_READ_LE32(eocd + 12); // cd_size
    mz_uint32 cd_ofs = MZ_READ_LE32(eocd + 16);

    zip_internal_state *state = (zip_internal_state *)calloc(1, sizeof(zip_internal_state));
    if (!state) {
        fclose(f);
        return MZ_FALSE;
    }

    state->f = f;
    state->num_entries = total_entries;
    state->entries = (zip_entry_internal *)calloc(total_entries, sizeof(zip_entry_internal));

    fseek(f, cd_ofs, SEEK_SET);
    for (mz_uint32 i = 0; i < total_entries; i++) {
        mz_uint8 chdr[46];
        if (fread(chdr, 1, 46, f) != 46 || MZ_READ_LE32(chdr) != 0x02014b50) {
            break;
        }
        state->entries[i].method = MZ_READ_LE16(chdr + 10);
        state->entries[i].crc32 = MZ_READ_LE32(chdr + 16);
        state->entries[i].comp_size = MZ_READ_LE32(chdr + 20);
        state->entries[i].uncomp_size = MZ_READ_LE32(chdr + 24);
        mz_uint16 fn_len = MZ_READ_LE16(chdr + 28);
        mz_uint16 extra_len = MZ_READ_LE16(chdr + 30);
        mz_uint16 comment_len = MZ_READ_LE16(chdr + 32);
        state->entries[i].local_header_ofs = MZ_READ_LE32(chdr + 42);

        if (fn_len < MAX_PATH) {
            fread(state->entries[i].filename, 1, fn_len, f);
            state->entries[i].filename[fn_len] = '\0';
        } else {
            fseek(f, fn_len, SEEK_CUR);
        }

        size_t slen = strlen(state->entries[i].filename);
        if (slen > 0 && (state->entries[i].filename[slen - 1] == '/' || state->entries[i].filename[slen - 1] == '\\')) {
            state->entries[i].is_dir = MZ_TRUE;
        }

        fseek(f, extra_len + comment_len, SEEK_CUR);
    }

    pZip->m_total_files = total_entries;
    pZip->m_pState = state;
    pZip->m_zip_mode = MZ_ZIP_MODE_READING;
    return MZ_TRUE;
}

mz_bool mz_zip_reader_end(mz_zip_archive *pZip) {
    if (!pZip || !pZip->m_pState) return MZ_FALSE;
    zip_internal_state *state = (zip_internal_state *)pZip->m_pState;
    if (state->f) fclose(state->f);
    if (state->entries) free(state->entries);
    free(state);
    memset(pZip, 0, sizeof(*pZip));
    return MZ_TRUE;
}

mz_uint32 mz_zip_reader_get_num_files(mz_zip_archive *pZip) {
    return pZip ? pZip->m_total_files : 0;
}

mz_bool mz_zip_reader_file_stat(mz_zip_archive *pZip, mz_uint32 file_index, mz_zip_archive_file_stat *pStat) {
    if (!pZip || !pZip->m_pState || !pStat) return MZ_FALSE;
    zip_internal_state *state = (zip_internal_state *)pZip->m_pState;
    if (file_index >= state->num_entries) return MZ_FALSE;

    memset(pStat, 0, sizeof(*pStat));
    pStat->m_file_index = file_index;
    pStat->m_method = state->entries[file_index].method;
    pStat->m_comp_size = state->entries[file_index].comp_size;
    pStat->m_uncomp_size = state->entries[file_index].uncomp_size;
    pStat->m_crc32 = state->entries[file_index].crc32;
    pStat->m_is_directory = state->entries[file_index].is_dir;
    strncpy_s(pStat->m_filename, sizeof(pStat->m_filename), state->entries[file_index].filename, _TRUNCATE);
    return MZ_TRUE;
}

mz_bool mz_zip_reader_is_file_a_directory(mz_zip_archive *pZip, mz_uint32 file_index) {
    if (!pZip || !pZip->m_pState) return MZ_FALSE;
    zip_internal_state *state = (zip_internal_state *)pZip->m_pState;
    if (file_index >= state->num_entries) return MZ_FALSE;
    return state->entries[file_index].is_dir;
}

void *mz_zip_reader_extract_file_to_heap(mz_zip_archive *pZip, mz_uint32 file_index, size_t *pSize, mz_uint32 flags) {
    (void)flags;
    if (!pZip || !pZip->m_pState) return NULL;
    zip_internal_state *state = (zip_internal_state *)pZip->m_pState;
    if (file_index >= state->num_entries) return NULL;

    zip_entry_internal *ent = &state->entries[file_index];
    if (ent->is_dir) return NULL;

    fseek(state->f, (long)ent->local_header_ofs, SEEK_SET);
    mz_uint8 lhdr[30];
    if (fread(lhdr, 1, 30, state->f) != 30 || MZ_READ_LE32(lhdr) != 0x04034b50) {
        return NULL;
    }

    mz_uint16 fn_len = MZ_READ_LE16(lhdr + 26);
    mz_uint16 extra_len = MZ_READ_LE16(lhdr + 28);
    fseek(state->f, fn_len + extra_len, SEEK_CUR);

    mz_uint8 *comp_data = (mz_uint8 *)malloc((size_t)ent->comp_size);
    if (!comp_data) return NULL;

    if (fread(comp_data, 1, (size_t)ent->comp_size, state->f) != ent->comp_size) {
        free(comp_data);
        return NULL;
    }

    mz_uint8 *uncomp_data = (mz_uint8 *)malloc((size_t)ent->uncomp_size + 1);
    if (!uncomp_data) {
        free(comp_data);
        return NULL;
    }

    if (ent->method == 0) {
        // Stored
        memcpy(uncomp_data, comp_data, (size_t)ent->uncomp_size);
    } else if (ent->method == 8) {
        // Deflated
        if (!inflate_raw(comp_data, (size_t)ent->comp_size, uncomp_data, (size_t)ent->uncomp_size)) {
            free(comp_data);
            free(uncomp_data);
            return NULL;
        }
    } else {
        free(comp_data);
        free(uncomp_data);
        return NULL;
    }

    free(comp_data);

    // Verify CRC
    mz_uint32 calc_crc = mz_crc32(0, uncomp_data, (size_t)ent->uncomp_size);
    if (calc_crc != ent->crc32) {
        free(uncomp_data);
        return NULL;
    }

    uncomp_data[ent->uncomp_size] = '\0';
    if (pSize) *pSize = (size_t)ent->uncomp_size;
    return uncomp_data;
}

mz_bool mz_zip_reader_extract_to_file(mz_zip_archive *pZip, mz_uint32 file_index, const char *pDst_filename, mz_uint32 flags) {
    size_t sz = 0;
    void *data = mz_zip_reader_extract_file_to_heap(pZip, file_index, &sz, flags);
    if (!data && sz > 0) return MZ_FALSE;

    FILE *f = NULL;
    if (fopen_s(&f, pDst_filename, "wb") != 0 || !f) {
        if (data) free(data);
        return MZ_FALSE;
    }

    if (sz > 0 && data) {
        fwrite(data, 1, sz, f);
    }
    fclose(f);
    if (data) free(data);
    return MZ_TRUE;
}
