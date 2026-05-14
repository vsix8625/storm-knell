#include "sk_xxhash.h"
#include "mem_arena.h"
#include "sk_eval.h"
#include "sk_util.h"
#include "sk_globals.h"
#include "sk_config.h"
#include "sk_array.h"

#include "vx_io.h"
#include "vx_fs.h"
#include <ctype.h>

#define XXH_STATIC_LINKING_ONLY
#define XXH_HASH_INLINE_ALL
#include <xxhash.h>

#define SK_MAX_SEEN_INCLUDES 1024

static_assert(SK_XXHASH_LEN == sizeof(u64));

static void sk_scan_inc(const char            *src_path,
                        vx_sv                  src,
                        struct sk_arena_array *seen,
                        struct sk_cfg         *cfg,
                        XXH3_state_t          *state);

vx_status sk_xxh3_hash(struct sk_hash_input *input, u8 out_hash[SK_XXHASH_LEN])
{
    if (input == nullptr)
    {
        return VX_ERROR;
    }

    XXH3_state_t state;

    if (XXH3_64bits_reset(&state) == XXH_ERROR)
    {
        return VX_ERROR;
    }

    if (XXH3_64bits_update(&state, input->source.data, input->source.len) == XXH_ERROR)
    {
        return VX_ERROR;
    }

    if (XXH3_64bits_update(&state, input->cmd.data, input->cmd.len) == XXH_ERROR)
    {
        return VX_ERROR;
    }

    if (XXH3_64bits_update(&state, input->sk_version.data, input->sk_version.len) == XXH_ERROR)
    {
        return VX_ERROR;
    }

    struct sk_arena_array *seen = sk_arena_array_create(g_sk_global_arena, SK_MAX_SEEN_INCLUDES);
    sk_scan_inc(input->source_path, input->source, seen, input->cfg, &state);

    XXH64_hash_t hash = XXH3_64bits_digest(&state);
    memcpy(out_hash, &hash, SK_XXHASH_LEN);

    return VX_OK;
}

void sk_xxh3_hash_merge(u8 h1[SK_XXHASH_LEN], u8 h2[SK_XXHASH_LEN], u8 out[SK_XXHASH_LEN])
{
    XXH3_state_t state;

    if (XXH3_64bits_reset(&state) == XXH_ERROR)
    {
        return;
    }

    if (XXH3_64bits_update(&state, h1, SK_XXHASH_LEN) == XXH_ERROR)
    {
        return;
    }

    if (XXH3_64bits_update(&state, h2, SK_XXHASH_LEN) == XXH_ERROR)
    {
        return;
    }

    XXH64_hash_t hash = XXH3_64bits_digest(&state);
    memcpy(out, &hash, SK_XXHASH_LEN);
}

struct sk_hash_input *sk_hash_input_create(void)
{
    struct sk_hash_input *hash_input =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_hash_input));

    if (hash_input == nullptr)
    {
        return nullptr;
    }

    hash_input->cfg = mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_cfg));

    if (hash_input->cfg == nullptr)
    {
        return nullptr;
    }

    return hash_input;
}

//----------------------------------------------------------------------------------------------------
// hash includes

static void sk_scan_inc(const char            *src_path,
                        vx_sv                  src,
                        struct sk_arena_array *seen,
                        struct sk_cfg         *cfg,
                        XXH3_state_t          *state)
{
    if (src_path == nullptr || seen == nullptr || cfg == nullptr || state == nullptr)
    {
        return;
    }

    const char *p   = src.data;
    const char *end = src.data + src.len;

    while (p < end)
    {
        const char *line = p;

        while (p < end && *p != CHAR_NEWLINE)
        {
            p++;
        }

        p++;

        while (line < p && isspace((u8) *line))
        {
            line++;
        }

        if (strncmp(line, "#include \"", 10) != 0)
        {
            continue;
        }

        line += 10;

        const char *inc_start = line;

        while (line < p && *line != CHAR_DOUBLE_QUOTE)
        {
            line++;
        }

        if (line == inc_start)
        {
            continue;
        }

        char resolved[VX_PATH_MAX];
        bool found = false;

        const char *last_sep = strrchr(src_path, VX_PATH_SEP);

        if (last_sep)
        {
            size_t dir_len = last_sep - src_path + 1;
            memcpy(resolved, src_path, dir_len);
            memcpy(resolved + dir_len, inc_start, line - inc_start);
            resolved[dir_len + (line - inc_start)] = CHAR_NULTERM;

            if (vx_isfile(resolved))
            {
                found = true;
            }
        }

        if (!found)
        {
            for (u32 i = 0; i < cfg->includes_count; i++)
            {
                const char *ipath = cfg->includes[i] + 2;  // skip -I
                snprintf(resolved,
                         VX_PATH_MAX,
                         "%s%s%.*s",
                         ipath,
                         VX_PATH_SEP_STR,
                         (i32) (line - inc_start),
                         inc_start);
                if (vx_isfile(resolved))
                {
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            continue;
        }

        if (sk_arena_array_contains(seen, resolved))
        {
            continue;
        }
        sk_arena_array_push(seen, sv_to_arena(g_sk_global_arena, vx_sv_from_cstr(resolved)));

        vx_sv inc_sv = vx_fs_read(resolved, sk_arena_alloc, g_sk_global_arena);
        if (inc_sv.data == nullptr)
        {
            continue;
        }

        XXH3_64bits_update(state, inc_sv.data, inc_sv.len);

        sk_scan_inc(resolved, inc_sv, seen, cfg, state);
    }
}

//----------------------------------------------------------------------------------------------------

vx_status sk_hash_setup(struct sk_target     *t,
                        u32                   source_idx,
                        struct sk_hash_input *hsh_input,
                        u8                    out_hash[SK_XXHASH_LEN])
{
    const char *src_path = (const char *) t->sources->items[source_idx];

    hsh_input->source_path = src_path;
    hsh_input->cfg         = &t->cfg;
    hsh_input->sk_version  = vx_sv_from_cstr(SK_VERSION_STRING);
    hsh_input->source      = vx_fs_read(src_path, sk_arena_alloc, g_sk_global_arena);

    size_t buf_stride = VX_PATH_MAX * 2;
    char  *h_buf      = mem_arena_alloc(g_sk_global_arena, buf_stride);

    i32 written = snprintf(h_buf, buf_stride, "%s", t->cfg.cc);

    for (u32 i = 0; i < t->cfg.cflags_count; i++)
    {
        written += snprintf(h_buf + written, buf_stride - written, " %s", t->cfg.cflags[i]);
    }

    for (u32 i = 0; i < t->cfg.includes_count; i++)
    {
        written += snprintf(h_buf + written, buf_stride - written, " %s", t->cfg.includes[i]);
    }

    for (u32 i = 0; i < t->cfg.defines_count; i++)
    {
        written += snprintf(h_buf + written, buf_stride - written, " %s", t->cfg.defines[i]);
    }

    hsh_input->cmd = vx_sv_from_cstr(h_buf);

    return sk_xxh3_hash(hsh_input, out_hash);
}
