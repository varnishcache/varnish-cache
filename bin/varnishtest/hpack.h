#include <stdint.h>

enum hpk_result{
	hpk_more = 0,
	hpk_done,
	hpk_err,
};

enum hpk_indexed {
	hpk_unset = 0,
	hpk_idx,
	hpk_inc,
	hpk_not,
	hpk_never,
};

struct txt {
	char *ptr;
	int len;
	int huff;
};

struct hpk_hdr {
	struct txt key;
	struct txt value;
	enum hpk_indexed t;
	int i;
};

struct hpk_ctx;
struct hpk_iter;

struct hpk_ctx * HPK_NewCtx(uint32_t tblsize);
void HPK_FreeCtx(struct hpk_ctx *ctx);

struct hpk_iter * HPK_NewIter(struct hpk_ctx *ctx, void *buf, int size);
void HPK_FreeIter(struct hpk_iter *iter);

enum hpk_result HPK_DecHdr(struct hpk_iter *iter, struct hpk_hdr *header);
enum hpk_result HPK_EncHdr(struct hpk_iter *iter, const struct hpk_hdr *header);

int gethpk_iterLen(const struct hpk_iter *iter);

enum hpk_result HPK_ResizeTbl(struct hpk_ctx *ctx, uint32_t num);

const struct hpk_hdr * HPK_GetHdr(const struct hpk_ctx *ctx, uint32_t index);

uint32_t HPK_GetTblSize(const struct hpk_ctx *ctx);
uint32_t HPK_GetTblMaxSize(const struct hpk_ctx *ctx);
uint32_t HPK_GetTblLength(const struct hpk_ctx *ctx);

#if 0
/* DEBUG */
void dump_dyn_tbl(const struct hpk_ctx *ctx);
#endif
