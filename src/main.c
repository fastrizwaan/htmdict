#include <gtk/gtk.h>
#include <adwaita.h>
#include <webkit/webkit.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <ctype.h>

#include "htm-dict.h"
#include "dict-fts-index.h"
#include "flat-index.h"

static GPtrArray *cli_paths;
static void app_state_free(gpointer p);

typedef struct {
    AdwApplication  *app;
    GPtrArray       *dicts;
    guint            current;
    GtkDropDown     *dict_dropdown;
    GtkSearchEntry  *search;
    GtkListBox      *list;
    WebKitWebView   *webview;
    GArray          *filter_indices; /* array of size_t (flat-index positions) */
    gboolean         fts_enabled;
    AdwStyleManager *style_mgr;
    gboolean         is_dark;
    char            *last_loaded_word;
    GtkWidget       *progress_dialog;
    GtkProgressBar  *progress_bar;
    GtkLabel        *progress_label;
    GtkButton       *theme_toggle;
} AppState;

typedef struct {
    AppState *app;
    GPtrArray *paths;
} LoadThreadData;

typedef struct {
    AppState *app;
    char *msg;
    double progress;
} ProgressInfo;

typedef struct {
    AppState *app;
    HtmDict *dict;
} DictInfo;

static void rebuild_list(AppState *app);

static ProgressInfo* g_new_progress_info(AppState *app, const char *msg, double progress) {
    ProgressInfo *pi = g_new(ProgressInfo, 1);
    pi->app = app;
    pi->msg = g_strdup(msg);
    pi->progress = progress;
    return pi;
}

static void g_progress_info_free(ProgressInfo *pi) {
    g_free(pi->msg);
    g_free(pi);
}

static DictInfo* g_new_dict_info(AppState *app, HtmDict *dict) {
    DictInfo *di = g_new(DictInfo, 1);
    di->app = app;
    di->dict = dict;
    return di;
}

static void g_dict_info_free(DictInfo *di) {
    g_free(di);
}

static gboolean update_progress_ui(ProgressInfo *pi) {
    if (pi->app->progress_label)
        gtk_label_set_text(pi->app->progress_label, pi->msg);
    if (pi->app->progress_bar)
        gtk_progress_bar_set_fraction(pi->app->progress_bar, pi->progress);
    g_progress_info_free(pi);
    return G_SOURCE_REMOVE;
}

static gboolean add_dict_to_app(DictInfo *di) {
    g_ptr_array_add(di->app->dicts, di->dict);
    GListModel *model = gtk_drop_down_get_model(di->app->dict_dropdown);
    if (GTK_IS_STRING_LIST(model)) {
        gtk_string_list_append(GTK_STRING_LIST(model), htm_dict_display_name(di->dict));
    }
    if (di->app->dicts->len == 1) {
        gtk_drop_down_set_selected(di->app->dict_dropdown, 0);
        di->app->current = 0;
        rebuild_list(di->app);
    }
    g_dict_info_free(di);
    return G_SOURCE_REMOVE;
}

static gboolean loading_finished(AppState *app) {
    if (app->progress_dialog) {
        gtk_window_destroy(GTK_WINDOW(app->progress_dialog));
        app->progress_dialog = NULL;
    }
    return G_SOURCE_REMOVE;
}

static gpointer load_dicts_worker(gpointer data) {
    LoadThreadData *ltd = data;
    AppState *app = ltd->app;
    for (guint i = 0; i < ltd->paths->len; i++) {
        char *path = g_ptr_array_index(ltd->paths, i);
        char *msg = g_strdup_printf("Loading %s...", g_path_get_basename(path));
        g_idle_add((GSourceFunc)update_progress_ui, g_new_progress_info(app, msg, (double)i / ltd->paths->len));
        g_free(msg);
        GError *err = NULL;
        HtmDict *d = htm_dict_open(path, &err);
        if (d) {
            g_idle_add((GSourceFunc)add_dict_to_app, g_new_dict_info(app, d));
        } else {
            g_warning("Failed to open %s: %s", path, err ? err->message : "unknown");
            g_clear_error(&err);
        }
    }
    g_idle_add((GSourceFunc)loading_finished, app);
    g_ptr_array_unref(ltd->paths);
    g_free(ltd);
    return NULL;
}

static HtmDict *app_active_dict(AppState *app) {
    if (!app || app->dicts->len == 0) return NULL;
    return g_ptr_array_index(app->dicts, app->current);
}

/* ── theme helpers ─────────────────────────────────────────────── */
static gboolean current_is_dark(AppState *app) {
    return adw_style_manager_get_dark(app->style_mgr);
}

/* ── config helpers ───────────────────────────────────────────── */
static char *app_config_dir_path(void) {
    return g_build_filename(g_get_user_config_dir(), "htmdict", NULL);
}

static char *app_config_file_path(void) {
    char *dir = app_config_dir_path();
    char *path = g_build_filename(dir, "config.ini", NULL);
    g_free(dir);
    return path;
}

static void load_config(AppState *app) {
    char *path = app_config_file_path();
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;

    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        char *scheme = g_key_file_get_string(kf, "ui", "color-scheme", NULL);
        if (g_strcmp0(scheme, "dark") == 0)
            adw_style_manager_set_color_scheme(app->style_mgr, ADW_COLOR_SCHEME_PREFER_DARK);
        else if (g_strcmp0(scheme, "light") == 0)
            adw_style_manager_set_color_scheme(app->style_mgr, ADW_COLOR_SCHEME_PREFER_LIGHT);
        app->fts_enabled = g_key_file_get_boolean(kf, "search", "fts-enabled", NULL);
        g_free(scheme);
    } else {
        g_clear_error(&err);
    }

    g_key_file_unref(kf);
    g_free(path);
}

static void save_config(AppState *app) {
    char *dir = app_config_dir_path();
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_free(dir);
        return;
    }

    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, "ui", "color-scheme", current_is_dark(app) ? "dark" : "light");
    g_key_file_set_boolean(kf, "search", "fts-enabled", app->fts_enabled);

    gsize len = 0;
    char *data = g_key_file_to_data(kf, &len, NULL);
    char *path = g_build_filename(dir, "config.ini", NULL);
    g_file_set_contents(path, data, len, NULL);

    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
    g_free(dir);
}

static const char *theme_css(gboolean dark) {
    if (dark) {
        return "html, body { background: #1e1e1e !important; color: #e0e0e0 !important; } "
               "body { font-family: sans-serif; padding: 20px; line-height: 1.6; color-scheme: dark; } "
               "a { color: #8ab4f8 !important; } "
               "font[color], span[style*='color'], div[style*='color'], p[style*='color'] { color: inherit !important; } "
               "*:not(img):not(svg):not(canvas):not(video) { background-color: transparent !important; }";
    } else {
        return "html, body { background: #fafafa !important; color: #1a1a1a !important; } "
               "body { font-family: sans-serif; padding: 20px; line-height: 1.6; color-scheme: light; } "
               "a { color: #0066cc !important; }";
    }
}

/* Inject CSS into running page — no reload */
static void inject_theme(AppState *app) {
    gboolean dark = current_is_dark(app);
    const char *css = theme_css(dark);

    /* 1. Set as user stylesheet for future loads */
    WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(app->webview);
    webkit_user_content_manager_remove_all_style_sheets(ucm);
    WebKitUserStyleSheet *uss = webkit_user_style_sheet_new(
        css, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_STYLE_LEVEL_USER, NULL, NULL);
    webkit_user_content_manager_add_style_sheet(ucm, uss);
    webkit_user_style_sheet_unref(uss);

    /* 2. Update current page via JS */
    char *escaped_css = g_strescape(css, NULL);
    char *js = g_strdup_printf(
        "(function(){"
        "var parent=document.head||document.documentElement;"
        "if(!parent)return;"
        "var s=document.getElementById('__htmdict_theme');"
        "if(!s){s=document.createElement('style');s.id='__htmdict_theme';parent.appendChild(s);}"
        "s.textContent=\"%s\";"
        "})()", escaped_css);
    webkit_web_view_evaluate_javascript(app->webview, js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js); g_free(escaped_css);
}

static void on_dark_changed(AdwStyleManager *mgr, GParamSpec *ps, gpointer ud) {
    (void)mgr; (void)ps;
    AppState *app = ud;
    app->is_dark  = current_is_dark(app);
    if (app->theme_toggle) {
        gtk_button_set_icon_name(app->theme_toggle,
            app->is_dark ? "display-brightness-symbolic" : "night-light-symbolic");
    }
    inject_theme(app);
    save_config(app);
}

static void on_theme_toggled(GtkButton *btn, gpointer ud) {
    (void)btn;
    AppState *app = ud;
    AdwColorScheme cs = adw_style_manager_get_color_scheme(app->style_mgr);
    if (cs == ADW_COLOR_SCHEME_PREFER_DARK) {
        adw_style_manager_set_color_scheme(app->style_mgr, ADW_COLOR_SCHEME_PREFER_LIGHT);
    } else {
        adw_style_manager_set_color_scheme(app->style_mgr, ADW_COLOR_SCHEME_PREFER_DARK);
    }
}

static char *normalize_for_search(const char *s) {
    if (!s) return NULL;
    GString *out = g_string_new("");
    const char *p = s;
    while (*p) {
        size_t len = g_utf8_skip[*(unsigned char *)p];
        /* Use a temporary copy of get_dsl_ignored_len_ext logic or export it */
        /* For now, let's just use the same symbols we know are ignored */
        gunichar ch = g_utf8_get_char(p);
        gboolean ignore = FALSE;
        if (g_unichar_isspace(ch) || ch == '*' || ch == '#' ||
            ch == 0x266F || ch == 0x266D || ch == 0x266E ||
            ch == 0x2191 || ch == 0x2193 || ch == 0x00B7 ||
            ch == 0x02C8 || ch == 0x02CC ||
            ch == 0x2018 || ch == 0x2019 || ch == 0x201C || ch == 0x201D ||
            ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
            ch == '{' || ch == '}' || 
            ch == '-' || ch == '\'' || ch == '`' || ch == '"' ||
            ch == ';' || ch == ':' || ch == '.' || ch == ',' ||
            ch == '!' || ch == '?' || ch == '_' || ch == '/' ||
            ch == '|' || ch == '~' ||
            g_unichar_type(ch) == G_UNICODE_NON_SPACING_MARK) {
            ignore = TRUE;
        }
        if (!ignore) g_string_append_len(out, p, len);
        p += len;
    }
    return g_string_free(out, FALSE);
}

/* ── page building ─────────────────────────────────────────────── */
static char *build_page(AppState *app, const char *body_html) {
    HtmDict    *d    = app_active_dict(app);
    const char *pfx  = d ? htm_dict_resource_prefix(d) : "";
    const char *id   = d ? htm_dict_id(d)               : "00000000";
    char *base = g_strdup_printf("htmdict:///%s/%s", id, pfx);
    char *css  = g_strdup(theme_css(current_is_dark(app)));
    char *page = g_strdup_printf(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<base href=\"%s\"/>"
        "<style id=\"__htmdict_theme\">%s</style>"
        "</head><body>%s</body></html>",
        base, css, body_html ? body_html : "<p><i>No entry.</i></p>");
    g_free(base); g_free(css);
    return page;
}

static void load_word(AppState *app, const char *word) {
    HtmDict *d = app_active_dict(app);
    if (!d || !word) return;
    char *norm = normalize_for_search(word);
    if (app->last_loaded_word && norm && g_strcmp0(app->last_loaded_word, norm) == 0) {
        g_free(norm);
        return;
    }
    char *html = htm_dict_get_definition_html(d, norm ? norm : word);
    char *page = build_page(app, html && html[0] ? html : "<p><i>No entry.</i></p>");
    char *base = g_strdup_printf("htmdict:///%s/%s", htm_dict_id(d), htm_dict_resource_prefix(d));
    webkit_web_view_load_html(app->webview, page, base);
    g_free(app->last_loaded_word);
    app->last_loaded_word = g_strdup(norm ? norm : word);
    g_free(page); g_free(base); g_free(html); g_free(norm);
}

static void load_exact_match_if_any(AppState *app) {
    HtmDict *d = app_active_dict(app);
    FlatIndex *fi = d ? htm_dict_get_flat_index(d) : NULL;
    if (!fi || flat_index_count(fi) == 0) return;

    const char *raw_q = gtk_editable_get_text(GTK_EDITABLE(app->search));
    char *q = normalize_for_search(raw_q);
    if (!q || !q[0]) {
        g_clear_pointer(&app->last_loaded_word, g_free);
        g_free(q);
        return;
    }

    size_t pos = flat_index_search(fi, q);
    if (pos != (size_t)-1) {
        const FlatTreeEntry *entry = flat_index_get(fi, pos);
        if (entry && flat_index_entry_matches_query(fi->mmap_data, entry, q, strlen(q)))
            load_word(app, q);
    }
    g_free(q);
}

/* ── URI scheme handler ────────────────────────────────────────── */
static void scheme_request_cb(WebKitURISchemeRequest *request, gpointer user_data) {
    AppState   *app = user_data;
    const char *uri = webkit_uri_scheme_request_get_uri(request);
    GUri *gu = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!gu) { webkit_uri_scheme_request_finish_error(request,
        g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "bad URI")); return; }
    const char *path = g_uri_get_path(gu);
    if (!path || path[0] != '/' || strlen(path) < 10) {
        g_uri_unref(gu); webkit_uri_scheme_request_finish_error(request,
            g_error_new_literal(G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "bad path")); return; }
    const char *p = path + 1;
    char idbuf[9]; memcpy(idbuf, p, 8); idbuf[8] = '\0';
    for (int i = 0; i < 8; i++) {
        if (!g_ascii_isxdigit(idbuf[i])) { g_uri_unref(gu);
            webkit_uri_scheme_request_finish_error(request,
                g_error_new_literal(G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "bad id")); return; } }
    if (p[8] != '/') { g_uri_unref(gu);
        webkit_uri_scheme_request_finish_error(request,
            g_error_new_literal(G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "bad path")); return; }
    const char *rel = p + 9;
    HtmDict *found = NULL;
    for (guint i = 0; i < app->dicts->len; i++) {
        HtmDict *d = g_ptr_array_index(app->dicts, i);
        if (strcmp(htm_dict_id(d), idbuf) == 0) { found = d; break; } }
    if (!found) { g_uri_unref(gu);
        webkit_uri_scheme_request_finish_error(request,
            g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "unknown dictionary")); return; }
    GError *err = NULL; GBytes *gb = NULL; char *mime = NULL;
    if (!htm_dict_read_resource_bytes(found, rel, &gb, &mime, &err)) {
        g_uri_unref(gu); webkit_uri_scheme_request_finish_error(request, err); return; }
    g_uri_unref(gu);
    gsize len = g_bytes_get_size(gb);
    GInputStream *in = g_memory_input_stream_new_from_bytes(gb);
    g_bytes_unref(gb);
    webkit_uri_scheme_request_finish(request, in, (gint64)len, mime ? mime : "application/octet-stream");
    g_object_unref(in); g_free(mime);
}

/* ── navigation / sound ───────────────────────────────────────── */
static gboolean policy_decide_cb(WebKitWebView *wv, WebKitPolicyDecision *decision,
                                 WebKitPolicyDecisionType type, gpointer user_data) {
    (void)wv;
    AppState *app = user_data;
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;
    WebKitNavigationPolicyDecision *nd = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    WebKitNavigationAction         *na = webkit_navigation_policy_decision_get_navigation_action(nd);
    WebKitURIRequest               *req = webkit_navigation_action_get_request(na);
    const char *uri = webkit_uri_request_get_uri(req);
    if (!uri) return FALSE;

    if (g_str_has_prefix(uri, "entry://")) {
        const char *raw = uri + strlen("entry://");
        while (*raw == '/' || *raw == ' ') raw++;
        char *unescaped = g_uri_unescape_string(raw, NULL);
        char *lw = g_utf8_strdown(unescaped ? unescaped : raw, -1);
        gtk_editable_set_text(GTK_EDITABLE(app->search), lw);
        load_word(app, lw);
        g_free(lw);
        g_free(unescaped);
        webkit_policy_decision_ignore(decision); return TRUE; }

    if (g_str_has_prefix(uri, "sound://")) {
        const char *fn = uri + strlen("sound://");
        while (*fn == '/' || *fn == ' ') fn++;
        HtmDict *d = app_active_dict(app);
        if (d) {
            char   *rel  = g_build_filename(htm_dict_resource_prefix(d), fn, NULL);
            GError *err  = NULL; GBytes *gb = NULL; char *mime = NULL;
            if (htm_dict_read_resource_bytes(d, rel, &gb, &mime, &err)) {
                GFileIOStream *ios = NULL;
                GFile *tmpf = g_file_new_tmp("htmdict-audio-XXXXXX", &ios, NULL);
                if (tmpf && ios) {
                    GOutputStream *os = g_io_stream_get_output_stream(G_IO_STREAM(ios));
                    gsize len = 0; const void *data = g_bytes_get_data(gb, &len);
                    if (g_output_stream_write_all(os, data, len, NULL, NULL, NULL)) {
                        g_output_stream_close(os, NULL, NULL);
                        char *path = g_file_get_path(tmpf);
                        gboolean is_spx = g_str_has_suffix(fn, ".spx") || g_str_has_suffix(fn, ".SPX");
                        guint buf_sz = is_spx ? 262144 : 65536;
                        char *uri_tmp  = g_filename_to_uri(path, NULL, NULL);
                        char *quri     = g_shell_quote(uri_tmp);
                        char *cmd      = g_strdup_printf(
                            "gst-launch-1.0 -q playbin uri=%s audio-sink='queue max-size-bytes=%u ! autoaudiosink'",
                            quri, buf_sz);
                        const char *argv[] = {"/bin/sh", "-c", cmd, NULL};
                        g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
                        g_free(cmd); g_free(quri); g_free(uri_tmp); g_free(path); }
                    g_object_unref(ios); g_object_unref(tmpf); }
                g_free(mime); g_bytes_unref(gb);
            } else g_clear_error(&err);
            g_free(rel); }
        webkit_policy_decision_ignore(decision); return TRUE; }
    return FALSE;
}

/* ── list rebuild using FlatIndex ─────────────────────────────── */
static void rebuild_list(AppState *app) {
    /* Remove old rows */
    for (;;) {
        GtkWidget *ch = gtk_widget_get_first_child(GTK_WIDGET(app->list));
        if (!ch) break;
        gtk_list_box_remove(app->list, ch);
    }
    g_array_set_size(app->filter_indices, 0);

    HtmDict    *d = app_active_dict(app);
    FlatIndex  *fi = d ? htm_dict_get_flat_index(d) : NULL;
    if (!fi || flat_index_count(fi) == 0) return;

    const char *raw_q = gtk_editable_get_text(GTK_EDITABLE(app->search));
    char *q = normalize_for_search(raw_q);
    gboolean    fts_hit = FALSE;

    if (q && q[0] && app->fts_enabled) {
        /* FTS query */
        GArray *candidates = dict_fts_query_candidates(htm_dict_zip_path(d), q, 0, 500);
        if (candidates) {
            fts_hit = TRUE;
            for (guint i = 0; i < candidates->len && i < 300; i++) {
                guint32 eid = g_array_index(candidates, guint32, i);
                size_t  pos = (size_t)eid;
                g_array_append_val(app->filter_indices, pos);
            }
            g_array_free(candidates, TRUE);
        }
    }

    if (!fts_hit) {
        /* Prefix/substring search via FlatIndex */
        size_t total = flat_index_count(fi);
        if (!q || !q[0]) {
            /* No query — show first 300 */
            guint limit = (guint)(total < 300 ? total : 300);
            for (guint i = 0; i < limit; i++) {
                size_t pos = (size_t)i;
                g_array_append_val(app->filter_indices, pos);
            }
        } else {
            /* Try prefix search first */
            size_t start = flat_index_search_prefix(fi, q);
            if (start != (size_t)-1) {
                for (size_t i = start; i < total && app->filter_indices->len < 300; i++) {
                    const FlatTreeEntry *e = flat_index_get(fi, i);
                    if (!flat_index_entry_matches_prefix(fi->mmap_data, e, q, strlen(q)))
                        break;
                    g_array_append_val(app->filter_indices, i);
                }
            }
            
            /* case-insensitive substring scan — capped at 300 */
            if (app->filter_indices->len < 300) {
                char *nd = g_utf8_strdown(q, -1);
                for (size_t i = 0; i < total && app->filter_indices->len < 300; i++) {
                    /* Avoid duplicates */
                    gboolean already = FALSE;
                    for (guint j = 0; j < app->filter_indices->len; j++) {
                        if (g_array_index(app->filter_indices, size_t, j) == i) {
                            already = TRUE; break;
                        }
                    }
                    if (already) continue;

                    const FlatTreeEntry *e = flat_index_get(fi, i);
                    if (!e) continue;
                    const char *hw  = fi->mmap_data + e->h_off;
                    char       *hwl = g_utf8_strdown(hw, (gssize)e->h_len);
                    if (strstr(hwl, nd))
                        g_array_append_val(app->filter_indices, i);
                    g_free(hwl);
                }
                g_free(nd);
            }
        }
    }
    g_free(q);

    /* Populate list box */
    for (guint i = 0; i < app->filter_indices->len; i++) {
        size_t               pos = g_array_index(app->filter_indices, size_t, i);
        const FlatTreeEntry *e   = flat_index_get(fi, pos);
        if (!e) continue;
        char *word = g_strndup(fi->mmap_data + e->h_off, e->h_len);
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(word);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 12); gtk_widget_set_margin_end(lbl, 12);
        gtk_widget_set_margin_top(lbl, 8);    gtk_widget_set_margin_bottom(lbl, 8);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        g_object_set_data_full(G_OBJECT(row), "word", word, g_free);
        gtk_list_box_append(app->list, row);
    }
}

static void on_search_changed(GtkSearchEntry *e, gpointer ud) {
    (void)e;
    AppState *app = ud;
    rebuild_list(app);
    load_exact_match_if_any(app);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer ud) {
    (void)box;
    AppState   *app = ud;
    const char *w   = g_object_get_data(G_OBJECT(row), "word");
    if (w) load_word(app, w);
}

static void on_dict_selected(GObject *obj, GParamSpec *pspec, gpointer ud) {
    (void)obj; (void)pspec;
    AppState *app = ud;
    guint     pos = gtk_drop_down_get_selected(app->dict_dropdown);
    if (pos >= app->dicts->len) return;
    app->current = pos;
    g_clear_pointer(&app->last_loaded_word, g_free);
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(app->webview));
    gtk_window_set_title(GTK_WINDOW(root), htm_dict_display_name(app_active_dict(app)));
    rebuild_list(app);
    load_exact_match_if_any(app);
}

/* ── settings menu ─────────────────────────────────────────────── */
static void on_fts_toggle(GObject *btn, GParamSpec *ps, gpointer ud) {
    (void)ps;
    AppState *app      = ud;
    app->fts_enabled   = gtk_check_button_get_active(GTK_CHECK_BUTTON(btn));
    rebuild_list(app);
    save_config(app);
}

static GtkWidget *build_settings_popover(AppState *app) {
    GtkWidget *box   = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);   gtk_widget_set_margin_bottom(box, 12);

    GtkWidget *lbl = gtk_label_new("<b>Settings</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    gtk_box_append(GTK_BOX(box), lbl);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget *fts_cb = gtk_check_button_new_with_label("Enable full-text search (FTS)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(fts_cb), app->fts_enabled);
    g_signal_connect(fts_cb, "notify::active", G_CALLBACK(on_fts_toggle), app);
    gtk_box_append(GTK_BOX(box), fts_cb);

    GtkWidget *fts_hint = gtk_label_new("FTS searches inside definitions.");
    gtk_label_set_xalign(GTK_LABEL(fts_hint), 0);
    gtk_widget_add_css_class(fts_hint, "caption");
    gtk_box_append(GTK_BOX(box), fts_hint);

    GtkWidget *pop = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(pop), box);
    return pop;
}

/* ── dict discovery ────────────────────────────────────────────── */
static gboolean filename_is_dict_archive(const char *name) {
    if (!name) return FALSE;
    const char *dot = strrchr(name, '.');
    if (!dot) return FALSE;
    return g_ascii_strcasecmp(dot, ".diction") == 0;
}

static void discover_dict_archives(const char *dir, GPtrArray *out) {
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) return;
    GDir *gd = g_dir_open(dir, 0, NULL); if (!gd) return;
    const char *name;
    while ((name = g_dir_read_name(gd))) {
        if (!filename_is_dict_archive(name)) continue;
        g_ptr_array_add(out, g_build_filename(dir, name, NULL));
    }
    g_dir_close(gd);
}

static int app_command_line(GApplication *application, GApplicationCommandLine *cmdline, gpointer ud) {
    (void)ud;
    gint argc = 0;
    gchar **args = g_application_command_line_get_arguments(cmdline, &argc);
    if (!args) return 1;
    if (!cli_paths) cli_paths = g_ptr_array_new_with_free_func(g_free);
    else            g_ptr_array_set_size(cli_paths, 0);
    for (gint i = 1; i < argc; i++) {
        if (filename_is_dict_archive(args[i])) g_ptr_array_add(cli_paths, g_strdup(args[i]));
        else discover_dict_archives(args[i], cli_paths);
    }
    g_strfreev(args);
    g_application_activate(application);
    return 0;
}

/* ── activate ──────────────────────────────────────────────────── */
static void app_activate(GtkApplication *gtk_app, gpointer ud) {
    (void)ud;
    AppState *app         = g_new0(AppState, 1);
    app->app              = ADW_APPLICATION(gtk_app);
    app->dicts            = g_ptr_array_new_with_free_func((GDestroyNotify)htm_dict_close);
    app->filter_indices   = g_array_new(FALSE, FALSE, sizeof(size_t));
    app->fts_enabled      = FALSE;
    app->style_mgr        = adw_style_manager_get_default();
    load_config(app);
    app->is_dark          = adw_style_manager_get_dark(app->style_mgr);

    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    if (cli_paths && cli_paths->len > 0) {
        for (guint i = 0; i < cli_paths->len; i++)
            g_ptr_array_add(paths, g_strdup(g_ptr_array_index(cli_paths, i)));
    } else {
        char *d1 = app_config_dir_path();
        discover_dict_archives(d1, paths);
        g_free(d1);
        discover_dict_archives(".", paths);
    }

    /* Start background loading */
    if (paths->len > 0) {
        app->progress_dialog = adw_window_new();
        gtk_window_set_title(GTK_WINDOW(app->progress_dialog), "Building Cache");
        gtk_window_set_default_size(GTK_WINDOW(app->progress_dialog), 400, 150);
        gtk_window_set_modal(GTK_WINDOW(app->progress_dialog), TRUE);
        
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(box, 24);
        gtk_widget_set_margin_bottom(box, 24);
        gtk_widget_set_margin_start(box, 24);
        gtk_widget_set_margin_end(box, 24);
        adw_window_set_content(ADW_WINDOW(app->progress_dialog), box);
        
        app->progress_label = GTK_LABEL(gtk_label_new("Starting..."));
        gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->progress_label));
        
        app->progress_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
        gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->progress_bar));
        
        LoadThreadData *ltd = g_new(LoadThreadData, 1);
        ltd->app = app;
        ltd->paths = paths; // worker will unref
        g_thread_new("loader", load_dicts_worker, ltd);
        
        gtk_window_present(GTK_WINDOW(app->progress_dialog));
    } else {
        g_ptr_array_unref(paths);
        g_warning("No dictionaries found.");
    }

    if (app->dicts->len == 0)
        g_warning("No dictionaries found.");

    GtkStringList *strs = gtk_string_list_new(NULL);
    for (guint i = 0; i < app->dicts->len; i++)
        gtk_string_list_append(strs, htm_dict_display_name(g_ptr_array_index(app->dicts, i)));

    GtkWidget *win = adw_application_window_new(GTK_APPLICATION(app->app));
    gtk_window_set_default_size(GTK_WINDOW(win), 1100, 750);

    AdwToolbarView *tb = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    AdwHeaderBar   *hb = ADW_HEADER_BAR(adw_header_bar_new());

    /* Dict dropdown as title widget */
    GtkWidget *ddw = gtk_drop_down_new(G_LIST_MODEL(strs), NULL);
    adw_header_bar_set_title_widget(hb, ddw);
    app->dict_dropdown = GTK_DROP_DOWN(ddw);
    g_signal_connect(app->dict_dropdown, "notify::selected", G_CALLBACK(on_dict_selected), app);

    /* Settings menu button (end) */
    app->theme_toggle = GTK_BUTTON(gtk_button_new_from_icon_name(
        app->is_dark ? "display-brightness-symbolic" : "night-light-symbolic"));
    g_signal_connect(app->theme_toggle, "clicked", G_CALLBACK(on_theme_toggled), app);
    adw_header_bar_pack_end(hb, GTK_WIDGET(app->theme_toggle));

    GtkWidget *pop     = build_settings_popover(app);
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_btn), pop);
    adw_header_bar_pack_end(hb, menu_btn);

    adw_toolbar_view_add_top_bar(tb, GTK_WIDGET(hb));

    /* Sidebar */
    GtkWidget *side  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    app->search = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_widget_set_margin_start(GTK_WIDGET(app->search), 12);
    gtk_widget_set_margin_end(GTK_WIDGET(app->search), 12);
    gtk_widget_set_margin_top(GTK_WIDGET(app->search), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(app->search), 12);
    g_signal_connect(app->search, "search-changed", G_CALLBACK(on_search_changed), app);

    app->list = GTK_LIST_BOX(gtk_list_box_new());
    g_signal_connect(app->list, "row-activated", G_CALLBACK(on_row_activated), app);
    GtkWidget *scr = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), GTK_WIDGET(app->list));
    gtk_widget_set_vexpand(scr, TRUE);
    gtk_box_append(GTK_BOX(side), GTK_WIDGET(app->search));
    gtk_box_append(GTK_BOX(side), scr);

    /* WebView */
    app->webview = WEBKIT_WEB_VIEW(webkit_web_view_new());
    WebKitWebContext *ctx = webkit_web_view_get_context(app->webview);
    webkit_web_context_register_uri_scheme(ctx, "htmdict", scheme_request_cb, app, NULL);
    WebKitSecurityManager *sm = webkit_web_context_get_security_manager(ctx);
    webkit_security_manager_register_uri_scheme_as_local(sm, "htmdict");
    webkit_security_manager_register_uri_scheme_as_cors_enabled(sm, "htmdict");
    {
        char *blank = build_page(app, "");
        webkit_web_view_load_html(app->webview, blank, "about:blank");
        g_free(blank);
    }
    g_signal_connect(app->webview, "decide-policy", G_CALLBACK(policy_decide_cb), app);
    inject_theme(app);

    /* Watch theme changes — inject CSS, no reload */
    g_signal_connect(app->style_mgr, "notify::dark", G_CALLBACK(on_dark_changed), app);

    /* Split view */
    AdwNavigationSplitView *split = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
    AdwNavigationPage *pg_side = adw_navigation_page_new(side, "Words");
    AdwNavigationPage *pg_def  = adw_navigation_page_new(GTK_WIDGET(app->webview), "Definition");
    adw_navigation_split_view_set_sidebar(split, pg_side);
    adw_navigation_split_view_set_content(split, pg_def);
    adw_navigation_split_view_set_sidebar_width_fraction(split, 0.28);

    adw_toolbar_view_set_content(tb, GTK_WIDGET(split));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), GTK_WIDGET(tb));
    g_object_set_data_full(G_OBJECT(win), "app-state", app, (GDestroyNotify)app_state_free);

    if (app->dicts->len > 0) {
        gtk_drop_down_set_selected(app->dict_dropdown, 0);
        app->current = 0;
        gtk_window_set_title(GTK_WINDOW(win), htm_dict_display_name(app_active_dict(app)));
        rebuild_list(app);
    } else {
        gtk_window_set_title(GTK_WINDOW(win), "htmdict");
    }

    gtk_window_present(GTK_WINDOW(win));
}

static void app_state_free(gpointer p) {
    AppState *app = p;
    if (!app) return;
    g_ptr_array_unref(app->dicts);
    g_array_unref(app->filter_indices);
    g_free(app->last_loaded_word);
    g_free(app);
}

int main(int argc, char **argv) {
    (void)argv;
    adw_init();
    GtkApplication *app = gtk_application_new("io.github.fastrizwaan.htmdict",
                                              G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "command-line", G_CALLBACK(app_command_line), NULL);
    g_signal_connect(app, "activate",     G_CALLBACK(app_activate),     NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
