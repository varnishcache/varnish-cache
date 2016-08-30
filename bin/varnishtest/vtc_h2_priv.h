#include "vqueue.h"

#define ITER_DONE(iter) (iter->buf == iter->end ? hpk_done : hpk_more)

struct dynhdr {
	struct hpk_hdr header;
	VTAILQ_ENTRY(dynhdr)      list;
};

VTAILQ_HEAD(dynamic_table,dynhdr);

struct hpk_iter {
	struct hpk_ctx *ctx;
	char *orig;
	char *buf;
	char *end;
};

const struct txt * tbl_get_key(const struct hpk_ctx *ctx, uint32_t index);

const struct txt * tbl_get_value(const struct hpk_ctx *ctx, uint32_t index);
void push_header (struct hpk_ctx *ctx, const struct hpk_hdr *h);
