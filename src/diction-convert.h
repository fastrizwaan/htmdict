#ifndef DICTION_CONVERT_H
#define DICTION_CONVERT_H

#include <glib.h>
#include <stdio.h>
#include <stdint.h>

typedef struct {
    char *output_root;
    char *dict_dir;
    char *dict_name;
    char *html_name;
    char *html_path;
    char *meta_path;
    char *css_path;
} DictionOutput;

DictionOutput *diction_output_new(const char *stem, const char *output_root);
void diction_output_free(DictionOutput *out);

char *diction_safe_name(const char *name);
void diction_print_html_text(FILE *out, const char *s);
void diction_print_html_attr(FILE *out, const char *s);
void diction_print_json_string(FILE *out, const char *s);
void diction_print_normalized_fragment(FILE *out, const uint8_t *data, size_t len);

gboolean diction_write_standard_css(const char *path, GError **error);
gboolean diction_write_meta(const char *path,
                            const char *id,
                            const char *name,
                            const char *short_name,
                            const char *html_name,
                            const char *index_languages,
                            const char *content_languages,
                            GError **error);
gboolean diction_package(const DictionOutput *out, GError **error);

#endif
