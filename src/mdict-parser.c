#include "mdict-parser.h"
#include "ripemd128.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <lzo/lzo1x.h>
#include <glib.h>
#include <arpa/inet.h>

struct _MdictReader {
    FILE *f;
    char *path;
    GHashTable *header;
    float version;
    char *encoding;
    uint32_t encrypt;
    uint32_t number_width;
    uint64_t num_entries;
    uint64_t key_block_offset;
    uint64_t record_block_offset;
    GPtrArray *key_list;
};

typedef struct {
    char *text;
    uint64_t offset;
} MdictKey;

static void mdict_key_free(gpointer p) {
    MdictKey *k = p;
    g_free(k->text);
    g_free(k);
}

static uint64_t read_uint32_be(FILE *f) {
    uint32_t val;
    if (fread(&val, 4, 1, f) != 1) return 0;
    return ntohl(val);
}

static uint64_t read_uint64_be(FILE *f) {
    uint64_t val;
    if (fread(&val, 8, 1, f) != 1) return 0;
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return __builtin_bswap64(val);
    #else
        return val;
    #endif
}

static uint64_t mdict_read_number(MdictReader *r, FILE *f) {
    if (r->number_width == 4) return read_uint32_be(f);
    return read_uint64_be(f);
}

static GHashTable* parse_mdict_header(const char *header_text) {
    GHashTable *tags = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const char *p = header_text;
    while ((p = strstr(p, " "))) {
        p++;
        const char *eq = strchr(p, '=');
        if (!eq) break;
        char *key = g_strndup(p, (gsize)(eq - p));
        const char *val_start = strchr(eq, '\"');
        if (!val_start) { g_free(key); break; }
        val_start++;
        const char *val_end = strchr(val_start, '\"');
        if (!val_end) { g_free(key); break; }
        char *val = g_strndup(val_start, (gsize)(val_end - val_start));
        g_hash_table_insert(tags, key, val);
        p = val_end + 1;
    }
    return tags;
}

static void mdict_fast_decrypt(uint8_t *data, size_t len, const uint8_t *key, size_t key_len) {
    uint8_t previous = 0x36;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        uint8_t t = ((b >> 4) | (b << 4)) & 0xff;
        t = t ^ previous ^ (uint8_t)(i & 0xff) ^ key[i % key_len];
        previous = b;
        data[i] = t;
    }
}

static uint8_t* decompress_block(uint8_t *comp, size_t comp_size, size_t decomp_size, uint32_t type) {
    uint8_t *decomp = g_malloc(decomp_size);
    int r = -1;
    if (type == 0 || type == 0x00000000) {
        memcpy(decomp, comp + 8, (decomp_size < (comp_size - 8)) ? decomp_size : (comp_size - 8));
        r = 0;
    } else if (type == 1 || type == 0x01000000) {
        lzo_uint out_len = decomp_size;
        r = lzo1x_decompress_safe(comp + 8, (lzo_uint)comp_size - 8, decomp, &out_len, NULL);
    } else if (type == 2 || type == 0x02000000) {
        unsigned long dlen = (unsigned long)decomp_size;
        r = uncompress(decomp, &dlen, comp + 8, (unsigned long)comp_size - 8);
    }
    
    if (r != 0) {
        g_free(decomp);
        return NULL;
    }
    return decomp;
}

MdictReader* mdict_reader_open(const char *path, GError **error) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not open %s", path);
        return NULL;
    }

    MdictReader *r = g_new0(MdictReader, 1);
    r->f = f;
    r->path = g_strdup(path);

    uint32_t header_size = (uint32_t)read_uint32_be(f);
    uint8_t *header_bytes = g_malloc(header_size);
    if (fread(header_bytes, 1, header_size, f) != header_size) {
        fprintf(stderr, "Error reading header bytes\n");
        mdict_reader_close(r); g_free(header_bytes);
        return NULL;
    }

    /* Adler32 after header */
    fseek(f, 4, SEEK_CUR);

    gsize bytes_read, bytes_written;
    char *header_utf8 = g_convert((const char*)header_bytes, (gssize)header_size, "UTF-8", "UTF-16LE", &bytes_read, &bytes_written, NULL);
    if (!header_utf8) {
        header_utf8 = g_convert((const char*)header_bytes, (gssize)header_size, "UTF-8", "UTF-16BE", &bytes_read, &bytes_written, NULL);
    }
    g_free(header_bytes);

    if (!header_utf8) {
        mdict_reader_close(r);
        return NULL;
    }

    r->header = parse_mdict_header(header_utf8);
    const char *ver_str = g_hash_table_lookup(r->header, "GeneratedByEngineVersion");
    r->version = ver_str ? (float)atof(ver_str) : 2.0f;
    
    const char *encoding = g_hash_table_lookup(r->header, "Encoding");
    if (!encoding || encoding[0] == '\0') {
        r->encoding = g_strdup(r->version >= 2.0f ? "UTF-16" : "GBK");
    } else {
        r->encoding = g_strdup(encoding);
    }

    const char *enc_str = g_hash_table_lookup(r->header, "Encrypted");
    if (enc_str) {
        if (g_ascii_strcasecmp(enc_str, "Yes") == 0) r->encrypt = 1;
        else if (g_ascii_strcasecmp(enc_str, "No") == 0) r->encrypt = 0;
        else r->encrypt = (uint32_t)atoi(enc_str);
    }

    r->number_width = (r->version < 2.0f) ? 4 : 8;
    r->key_block_offset = ftell(f);
    g_free(header_utf8);
    return r;
}

static gboolean mdict_reader_read_keys(MdictReader *r, GError **error) {
    fseek(r->f, r->key_block_offset, SEEK_SET);
    
    uint32_t num_bytes = (r->version >= 2.0f) ? 8 * 5 : 4 * 4;
    uint8_t *block = g_malloc(num_bytes);
    if (fread(block, 1, num_bytes, r->f) != num_bytes) {
        g_free(block); return FALSE;
    }

    uint64_t num_key_blocks = 0, num_entries = 0, key_block_info_decomp_size = 0, key_block_info_size = 0;
    if (r->version >= 2.0f) {
        memcpy(&num_key_blocks, block, 8); num_key_blocks = __builtin_bswap64(num_key_blocks);
        memcpy(&num_entries, block + 8, 8); num_entries = __builtin_bswap64(num_entries);
        memcpy(&key_block_info_decomp_size, block + 16, 8); key_block_info_decomp_size = __builtin_bswap64(key_block_info_decomp_size);
        memcpy(&key_block_info_size, block + 24, 8); key_block_info_size = __builtin_bswap64(key_block_info_size);
        /* Skip adler32 */
        fseek(r->f, 4, SEEK_CUR);
    } else {
        uint32_t tmp;
        memcpy(&tmp, block, 4); num_key_blocks = ntohl(tmp);
        memcpy(&tmp, block + 4, 4); num_entries = ntohl(tmp);
        memcpy(&tmp, block + 8, 4); key_block_info_size = ntohl(tmp);
    }
    r->num_entries = num_entries;

    uint8_t *kb_info_comp = g_malloc(key_block_info_size);
    if (fread(kb_info_comp, 1, key_block_info_size, r->f) != key_block_info_size) {
        g_free(kb_info_comp); return FALSE;
    }
    
    if (r->encrypt == 2) {
        uint8_t key[16];
        uint8_t ksrc[8];
        memcpy(ksrc, kb_info_comp + 4, 4); /* Adler checksum of compressed data */
        ksrc[4] = 0x95; ksrc[5] = 0x36; ksrc[6] = 0x00; ksrc[7] = 0x00;
        ripemd128(ksrc, 8, key);
        mdict_fast_decrypt(kb_info_comp + 8, key_block_info_size - 8, key, 16);
    }

    uint8_t *kb_info_decomp;
    if (r->version >= 2.0f) {
        uint32_t type;
        memcpy(&type, kb_info_comp, 4); type = ntohl(type);
        kb_info_decomp = decompress_block(kb_info_comp, key_block_info_size, (size_t)key_block_info_decomp_size, type);
    } else {
        kb_info_decomp = kb_info_comp; kb_info_comp = NULL;
    }
    if (!kb_info_decomp) {
        g_free(kb_info_comp);
        return FALSE;
    }

    typedef struct { uint64_t comp_size; uint64_t decomp_size; } KBInfo;
    KBInfo *kb_infos = g_new(KBInfo, (gsize)num_key_blocks);
    const uint8_t *p = kb_info_decomp;
    for (uint64_t i = 0; i < num_key_blocks; i++) {
        int width = (g_ascii_strcasecmp(r->encoding, "UTF-16") == 0) ? 2 : 1;
        p += r->number_width; /* Skip num_entries */
        if (r->version >= 2.0f) {
            uint16_t head_size; memcpy(&head_size, p, 2); head_size = ntohs(head_size);
            p += 2 + (size_t)head_size * width + width;
            uint16_t tail_size; memcpy(&tail_size, p, 2); tail_size = ntohs(tail_size);
            p += 2 + (size_t)tail_size * width + width;
        } else {
            p += 1 + *p; p += 1 + *p;
        }
        
        if (r->number_width == 4) {
            uint32_t tmp; memcpy(&tmp, p, 4); kb_infos[i].comp_size = ntohl(tmp);
            p += 4;
            memcpy(&tmp, p, 4); kb_infos[i].decomp_size = ntohl(tmp);
            p += 4;
        } else {
            uint64_t tmp; memcpy(&tmp, p, 8); kb_infos[i].comp_size = __builtin_bswap64(tmp) & 0x7FFFFFFFFFFFFFFFULL;
            p += 8;
            memcpy(&tmp, p, 8); kb_infos[i].decomp_size = __builtin_bswap64(tmp) & 0x7FFFFFFFFFFFFFFFULL;
            p += 8;
        }
    }
    if (r->version >= 2.0f) g_free(kb_info_decomp);
    g_free(kb_info_comp);

    r->key_list = g_ptr_array_new_full((guint)num_entries, mdict_key_free);
    for (uint64_t i = 0; i < num_key_blocks; i++) {
        uint8_t *comp_block = g_malloc(kb_infos[i].comp_size);
        if (fread(comp_block, 1, kb_infos[i].comp_size, r->f) != kb_infos[i].comp_size) {
            g_free(comp_block); continue;
        }
        uint32_t type; memcpy(&type, comp_block, 4); type = ntohl(type);
        uint8_t *decomp_block = decompress_block(comp_block, (size_t)kb_infos[i].comp_size, (size_t)kb_infos[i].decomp_size, type);
        g_free(comp_block);
        if (!decomp_block) continue;

        uint8_t *kp = decomp_block;
        uint8_t *kend = decomp_block + kb_infos[i].decomp_size;
        int width = (g_ascii_strcasecmp(r->encoding, "UTF-16") == 0) ? 2 : 1;

        while (kp < kend) {
            uint64_t offset;
            if (r->number_width == 4) {
                uint32_t tmp; memcpy(&tmp, kp, 4); offset = ntohl(tmp);
            } else {
                uint64_t tmp; memcpy(&tmp, kp, 8); offset = __builtin_bswap64(tmp);
            }
            kp += r->number_width;
            
            uint8_t *kstart = kp;
            while (kp < kend) {
                if (width == 1 && *kp == 0) break;
                if (width == 2 && kp + 1 < kend && *kp == 0 && *(kp + 1) == 0) break;
                kp += width;
            }
            
            MdictKey *k = g_new0(MdictKey, 1);
            k->offset = offset;
            if (width == 1) k->text = g_strndup((const char*)kstart, (gsize)(kp - kstart));
            else k->text = g_convert((const char*)kstart, (gssize)(kp - kstart), "UTF-8", "UTF-16LE", NULL, NULL, NULL);
            kp += width;

            if (k->text && k->text[0]) g_ptr_array_add(r->key_list, k);
            else mdict_key_free(k);
        }
        g_free(decomp_block);
    }
    g_free(kb_infos);
    r->record_block_offset = ftell(r->f);
    return TRUE;
}

gboolean mdict_reader_iterate(MdictReader *r, MdictEntryCallback callback, gpointer user_data, GError **error) {
    if (!r->key_list) {
        if (!mdict_reader_read_keys(r, error)) return FALSE;
    }

    fseek(r->f, r->record_block_offset, SEEK_SET);
    uint64_t num_record_blocks = mdict_read_number(r, r->f);
    uint64_t num_entries = mdict_read_number(r, r->f);
    uint64_t rb_info_size = mdict_read_number(r, r->f);
    uint64_t rb_size = mdict_read_number(r, r->f);
    (void)num_entries; (void)rb_size; (void)rb_info_size;

    typedef struct { uint64_t comp_size; uint64_t decomp_size; } RBInfo;
    RBInfo *rb_infos = g_new(RBInfo, (gsize)num_record_blocks);
    for (uint64_t i = 0; i < num_record_blocks; i++) {
        rb_infos[i].comp_size = mdict_read_number(r, r->f);
        rb_infos[i].decomp_size = mdict_read_number(r, r->f);
    }

    uint64_t current_key_idx = 0;
    uint64_t global_offset = 0;
    
    for (uint64_t i = 0; i < num_record_blocks; i++) {
        uint8_t *comp_block = g_malloc(rb_infos[i].comp_size);
        if (fread(comp_block, 1, rb_infos[i].comp_size, r->f) != rb_infos[i].comp_size) {
            g_free(comp_block); continue;
        }
        uint32_t type; memcpy(&type, comp_block, 4); type = ntohl(type);
        uint8_t *decomp_block = decompress_block(comp_block, (size_t)rb_infos[i].comp_size, (size_t)rb_infos[i].decomp_size, type);
        g_free(comp_block);
        if (!decomp_block) continue;
        
        while (current_key_idx < r->key_list->len) {
            MdictKey *k = g_ptr_array_index(r->key_list, current_key_idx);
            if (k->offset - global_offset >= rb_infos[i].decomp_size) break;
            
            uint64_t next_offset;
            if (current_key_idx + 1 < r->key_list->len) {
                next_offset = ((MdictKey*)g_ptr_array_index(r->key_list, current_key_idx + 1))->offset;
            } else {
                next_offset = global_offset + rb_infos[i].decomp_size;
            }
            
            size_t entry_len = (size_t)(next_offset - k->offset);
            if (k->offset + entry_len > global_offset + rb_infos[i].decomp_size) {
                entry_len = (size_t)(global_offset + rb_infos[i].decomp_size - k->offset);
            }
            
            if (entry_len > 0) {
                callback(k->text, decomp_block + (k->offset - global_offset), entry_len, user_data);
            }
            current_key_idx++;
        }
        global_offset += rb_infos[i].decomp_size;
        g_free(decomp_block);
    }
    g_free(rb_infos);
    return TRUE;
}

void mdict_reader_close(MdictReader *r) {
    if (!r) return;
    if (r->f) fclose(r->f);
    if (r->header) g_hash_table_destroy(r->header);
    g_free(r->encoding);
    g_free(r->path);
    if (r->key_list) g_ptr_array_unref(r->key_list);
    g_free(r);
}

const char* mdict_reader_get_title(MdictReader *r) {
    return g_hash_table_lookup(r->header, "Title");
}

const char* mdict_reader_get_encoding(MdictReader *r) {
    return r->encoding;
}

float mdict_reader_get_version(MdictReader *r) {
    return r->version;
}

uint64_t mdict_reader_get_num_entries(MdictReader *r) {
    return r->num_entries;
}
