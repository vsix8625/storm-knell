#ifndef SK_PATHS_H_
#define SK_PATHS_H_

#include "vx_defs.h"

struct sk_arena_array;

#define SK_PATH_STORM_DIR       ".storm"
#define SK_PATH_STORM_DATA_DIR  ".storm/data"
#define SK_PATH_STORM_TRASH_DIR ".storm/trash"
#define SK_PATH_STORM_CACHE_DIR ".storm/cache"
#define SK_PATH_STORM_LOCKFILE  ".storm/storm.lock"
#define SK_PATH_STORMFILE       "Stormfile"
#define SK_PATH_BUILD_DIR       "build"
#define SK_PATH_STORM_META_FILE ".storm/data/meta"

#define SK_PATH_STORM_KNELL "storm_knell"

void sk_scan_dir_r(struct sk_arena_array *sources,
                   char                 **excludes,
                   u32                    exclude_count,
                   const char            *dirpath);

void sk_path_strip_trailing_sep(char *path);

#endif  // SK_PATHS_H_
