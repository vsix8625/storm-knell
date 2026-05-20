#pragma once

#include "sk_config.h"

#define SK_VERSION_ENCODE(maj, min, pat) (((maj) << 22) | ((min) << 12) | (pat))
#define SK_VERSION_NUM SK_VERSION_ENCODE(SK_VERSION_MAJOR, SK_VERSION_MINOR, SK_VERSION_PATCH)
