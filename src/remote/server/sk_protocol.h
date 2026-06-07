#pragma once

#include "vx_defs.h"

#define SK_SERVER_PORT     8625
#define SK_SERVER_NAME_MAX 16
#define SK_SERVER_KEY_MAX  16
#define SK_CLIENT_UUID_MAX 32

typedef enum : u32
{
    SK_SERVER_CMD_AUTH = 0,

    SK_SERVER_CMD_AUTH_SUCCESS = 1 << 0,
    SK_SERVER_CMD_AUTH_FAILURE = 1 << 1,
    SK_SERVER_CMD_STRIKE       = 1 << 2,
    SK_SERVER_CMD_DISCONNECT   = 1 << 3,
} sk_server_cmd;

struct sk_packet_auth
{
    u32 cmd;

    char session_name[SK_SERVER_NAME_MAX];
    char session_key[SK_SERVER_KEY_MAX];
    char client_uuid[SK_CLIENT_UUID_MAX];
};

struct sk_packet_resp
{
    u32 cmd;
    u32 status;
};
