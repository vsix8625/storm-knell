#include "sk_cmd_new.h"
#include "sk_cli.h"

#include "vx_io.h"

vx_status sk_cmd_new_file(struct sk_ctx *ctx, const char *path)
{
    if (ctx == nullptr || path == nullptr)
    {
        return VX_ERROR;
    }

    return VX_OK;
}
