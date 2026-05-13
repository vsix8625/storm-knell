#ifndef SK_PIPELINE_H_
#define SK_PIPELINE_H_

#include "vx_defs.h"

struct sk_ctx;
struct sk_lexer;
struct sk_parser;
struct sk_eval_result;

vx_status sk_pipeline_run(struct sk_ctx         *ctx,
                          struct sk_lexer       *lx,
                          struct sk_parser      *p,
                          struct sk_eval_result *ev_result);

#endif  // SK_PIPELINE_H_
