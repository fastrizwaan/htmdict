#pragma once

#include <glib.h>

/* Cache layout: $XDG_CACHE_HOME/htmdict/dicts/<sha1-of-path> */

const char *dict_cache_base_dir(void);
char *dict_cache_dir_path(void);
char *dict_cache_path_for(const char *original_path);
gboolean dict_cache_is_valid(const char *cache_path, const char *original_path);
gboolean dict_cache_ensure_dir(void);
gboolean dict_cache_prepare_target_path(const char *target_path, guint64 bytes_needed);
void dict_cache_sync_mtime(const char *cache_path, const char **sources, int n_sources);
