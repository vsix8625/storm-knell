#include "sk_globals.h"

char g_sk_profile_buf[VX_PATH_MAX];

vx_sbuf g_sk_profile_sbuf = {.data   = g_sk_profile_buf,
                             .size   = sizeof(g_sk_profile_buf),
                             .offset = 0};
