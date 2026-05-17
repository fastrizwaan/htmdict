#include "diction-convert.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *diction_safe_name(const char *name) {
    if (!name || !name[0]) return g_strdup("Dictionary");

    GString *safe = g_string_new(NULL);
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (g_ascii_isalnum(*p) || *p == '-' || *p == '_') {
            g_string_append_c(safe, (char)*p);
        } else if (g_ascii_isspace(*p)) {
            g_string_append_c(safe, '_');
        }
    }

    if (safe->len == 0) g_string_append(safe, "Dictionary");
    return g_string_free(safe, FALSE);
}

DictionOutput *diction_output_new(const char *stem, const char *output_root) {
    DictionOutput *out = g_new0(DictionOutput, 1);
    out->dict_name = diction_safe_name(stem);
    out->html_name = g_strdup_printf("%s.html", out->dict_name);
    out->output_root = output_root ? g_strdup(output_root) : g_strdup_printf("%s_diction", out->dict_name);
    out->dict_dir = g_build_filename(out->output_root, out->dict_name, NULL);
    out->html_path = g_build_filename(out->dict_dir, out->html_name, NULL);
    out->meta_path = g_build_filename(out->dict_dir, "meta.json", NULL);
    out->css_path = g_build_filename(out->dict_dir, "style.css", NULL);
    return out;
}

void diction_output_free(DictionOutput *out) {
    if (!out) return;
    g_free(out->output_root);
    g_free(out->dict_dir);
    g_free(out->dict_name);
    g_free(out->html_name);
    g_free(out->html_path);
    g_free(out->meta_path);
    g_free(out->css_path);
    g_free(out);
}

void diction_print_html_text(FILE *out, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        switch (*p) {
        case '&': fputs("&amp;", out); break;
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        default: fputc(*p, out); break;
        }
    }
}

void diction_print_html_attr(FILE *out, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        switch (*p) {
        case '&': fputs("&amp;", out); break;
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        case '"': fputs("&quot;", out); break;
        case '\'': fputs("&#39;", out); break;
        default: fputc(*p, out); break;
        }
    }
}

void diction_print_json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        switch (*p) {
        case '"': fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\b': fputs("\\b", out); break;
        case '\f': fputs("\\f", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (*p < 0x20) fprintf(out, "\\u%04x", *p);
            else fputc(*p, out);
            break;
        }
    }
    fputc('"', out);
}

static gboolean starts_with_at(const uint8_t *p, const uint8_t *end, const char *needle) {
    size_t n = strlen(needle);
    return (size_t)(end - p) >= n && strncmp((const char *)p, needle, n) == 0;
}

void diction_print_normalized_fragment(FILE *out, const uint8_t *data, size_t len) {
    const uint8_t *p = data;
    const uint8_t *end = data + len;

    while (p < end) {
        if (starts_with_at(p, end, "src=\"/")) {
            fputs("src=\"media/", out);
            p += 6;
        } else if (starts_with_at(p, end, "src='/")) {
            fputs("src='media/", out);
            p += 6;
        } else if (starts_with_at(p, end, "href=\"/")) {
            fputs("href=\"media/", out);
            p += 7;
        } else if (starts_with_at(p, end, "href='/")) {
            fputs("href='media/", out);
            p += 7;
        } else if (starts_with_at(p, end, "<script") || starts_with_at(p, end, "<SCRIPT")) {
            const uint8_t *script_end = (const uint8_t *)g_strstr_len((const char *)p, end - p, "</script>");
            const uint8_t *script_end_upper = (const uint8_t *)g_strstr_len((const char *)p, end - p, "</SCRIPT>");
            if (!script_end || (script_end_upper && script_end_upper < script_end)) script_end = script_end_upper;
            p = script_end ? script_end + 9 : end;
        } else {
            fputc(*p, out);
            p++;
        }
    }
}

gboolean diction_write_standard_css(const char *path, GError **error) {
    static const char css[] =
        "body {\n"
        "  margin: 1rem;\n"
        "  font-family: system-ui, -apple-system, BlinkMacSystemFont, \"Segoe UI\", sans-serif;\n"
        "  line-height: 1.55;\n"
        "  color: #242424;\n"
        "  background: #ffffff;\n"
        "}\n"
        ".entry { margin: 0 0 1.5rem; padding: 18px; border: 1px solid #d0d0d0; border-radius: 8px; background: #f7f7f7; }\n"
        ".entry > header { margin-bottom: 0.45rem; }\n"
        "headword { display: block; margin: 0 0 0.45rem; font-size: 1.8rem; font-weight: 700; color: #9f2323; }\n"
        ".headword { margin: 0; font-size: 1.8rem; font-weight: 700; color: #9f2323; }\n"
        ".pronunciation { color: #4d6070; margin: 0.2rem 0 0.55rem; }\n"
        ".ipa { font-family: \"Charis SIL\", \"Noto Sans\", sans-serif; }\n"
        ".pos-group { margin: 0.65rem 0; }\n"
        ".pos { font-style: italic; font-weight: 600; color: #5b5b5b; }\n"
        ".definitions { margin-top: 0.25rem; }\n"
        ".examples { margin: 0.6rem 0; }\n"
        ".example { margin: 0.4rem 0; padding-left: 0.8rem; border-left: 3px solid #d6dde3; color: #334155; }\n"
        ".etymology, .notes { color: #555; }\n"
        ".translations { display: grid; gap: 0.35rem; margin: 0.6rem 0; }\n"
        ".translation::before { content: attr(lang); display: inline-block; min-width: 2.4rem; margin-right: 0.45rem; color: #697586; font-size: 0.82em; text-transform: uppercase; }\n"
        "a { color: #0f5f8f; text-decoration-thickness: 0.08em; }\n"
        "img, video { max-width: 100%; height: auto; }\n"
        "table { border-collapse: collapse; margin: 0.8rem 0; }\n"
        "th, td { border: 1px solid #d9dee3; padding: 0.35rem 0.5rem; }\n"
        "math { font-size: 1.5rem; }\n"
        "ol.upper-roman { list-style-type: upper-roman; }\n"
        "ol.lower-roman { list-style-type: lower-roman; }\n"
        "ol.lower-alpha { list-style-type: lower-alpha; }\n"
        "ol.upper-alpha { list-style-type: upper-alpha; }\n"
        "ol.decimal { list-style-type: decimal; }\n"
        "ol.decimal-paren, ol.lower-alpha-paren, ol.upper-alpha-paren { list-style: none; counter-reset: item; padding-left: 1.4rem; }\n"
        "ol.decimal-paren > li, ol.lower-alpha-paren > li, ol.upper-alpha-paren > li { counter-increment: item; }\n"
        "ol.decimal-paren > li::before { content: counter(item) \") \"; }\n"
        "ol.lower-alpha-paren > li::before { content: counter(item, lower-alpha) \") \"; }\n"
        "ol.upper-alpha-paren > li::before { content: counter(item, upper-alpha) \") \"; }\n"
        "ul.dash-list { list-style: none; padding-left: 1.2rem; }\n"
        "ul.dash-list li::before { content: \"- \"; }\n";

    if (!g_file_set_contents(path, css, -1, error)) return FALSE;
    return TRUE;
}

gboolean diction_write_meta(const char *path,
                            const char *id,
                            const char *name,
                            const char *short_name,
                            const char *html_name,
                            const char *index_languages,
                            const char *content_languages,
                            GError **error) {
    FILE *meta = fopen(path, "w");
    if (!meta) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create %s", path);
        return FALSE;
    }

    fputs("{\n  \"format\": 1,\n  \"id\": ", meta);
    diction_print_json_string(meta, id);
    fputs(",\n  \"name\": ", meta);
    diction_print_json_string(meta, name);
    fputs(",\n  \"short_name\": ", meta);
    diction_print_json_string(meta, short_name);
    fputs(",\n  \"index_languages\": ", meta);
    fputs(index_languages && index_languages[0] ? index_languages : "[\"en\"]", meta);
    fputs(",\n  \"content_languages\": ", meta);
    fputs(content_languages && content_languages[0] ? content_languages : "[\"en\"]", meta);
    char created[11] = "1970-01-01";
    time_t now = time(NULL);
    struct tm tm;
    if (gmtime_r(&now, &tm))
        strftime(created, sizeof(created), "%Y-%m-%d", &tm);

    fputs(",\n  \"version\": \"1.0\",\n  \"created\": ", meta);
    diction_print_json_string(meta, created);
    fputs(",\n  \"stylesheet\": \"style.css\",\n  \"html\": ", meta);
    diction_print_json_string(meta, html_name);
    fputs("\n}\n", meta);
    fclose(meta);
    return TRUE;
}

gboolean diction_package(const DictionOutput *out, GError **error) {
    char *cwd = g_get_current_dir();
    char *archive_name = g_strdup_printf("%s.diction", out->dict_name);
    char *archive_path = g_build_filename(cwd, archive_name, NULL);
    char *quoted_root = g_shell_quote(out->output_root);
    char *quoted_archive = g_shell_quote(archive_path);
    char *quoted_dict = g_shell_quote(out->dict_name);
    char *cmd = g_strdup_printf("cd %s && zip -rq %s %s", quoted_root, quoted_archive, quoted_dict);

    int status = system(cmd);
    g_free(cmd);
    g_free(quoted_root);
    g_free(quoted_archive);
    g_free(quoted_dict);
    g_free(archive_name);
    g_free(archive_path);
    g_free(cwd);

    if (status != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Failed to create .diction archive with zip");
        return FALSE;
    }
    return TRUE;
}
