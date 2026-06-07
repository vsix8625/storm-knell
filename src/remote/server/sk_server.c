#include "sk_server.h"
#include "sk_protocol.h"
#include "storm-knell.h"

#include "vx.h"

#include <arpa/inet.h>
#include <sys/socket.h>

// NYI
vx_status sk_server_init(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    i32 server_fd;
    i32 client_fd;

    i32 opt = 1;

    struct sockaddr_in address;

    socklen_t add_len = sizeof(address);

    // should be set on: sk server --session foo
    const char *expected_name = ctx->sk_server_ssname;
    const char *expected_key  = ctx->sk_server_sskey;

    vx_log("Starting session: %s", expected_name);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        VX_ASSERT_LOG("socket failed");
        return VX_ERROR;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        VX_ASSERT_LOG("setsockopt failed");
        return VX_ERROR;
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(SK_SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0)
    {
        VX_ASSERT_LOG("bind failed");
        return VX_ERROR;
    }

    if (listen(server_fd, 3) < 0)
    {
        VX_ASSERT_LOG("listen error");
        return VX_ERROR;
    }

    vx_log("Listening on port: %d", SK_SERVER_PORT);

    while (1)
    {
        if ((client_fd = accept(server_fd, (struct sockaddr *) &address, &add_len)) < 0)
        {
            VX_ASSERT_LOG("accept error");
            continue;
        }

        vx_log("Client connected. Reading auth packet...");

        struct sk_packet_auth auth = {0};

        ssize_t bytes_read = read(client_fd, &auth, sizeof(struct sk_packet_auth));

        if (bytes_read == sizeof(struct sk_packet_auth) && (auth.cmd & SK_SERVER_CMD_AUTH))
        {
            vx_log("[DEBUG]: Request received for session '%s' with key '%s'",
                   auth.session_name,
                   auth.session_key);

            struct sk_packet_resp resp = {0};
        }
    }
}
