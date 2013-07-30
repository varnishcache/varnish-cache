#include "cache.h"

int
KEY_Create(struct busyobj *bo, struct vsb **psb)
{
	printf("      KEY_Create(bo: %p, psb: %p)\n", bo, *psb);
	char *v;
	struct vsb *sb;

	if (!http_GetHdr(bo->beresp, "\4Key:", &v))
		return (0);

	sb = VSB_new_auto();
	AN(sb);
	VSB_printf(sb, "%s", v);
	AZ(VSB_finish(sb));
	*psb = sb;
	printf("      KEY_Create(bo: %p, psb: %p) = %d\n", bo, *psb, VSB_len(sb));
	return (VSB_len(sb));
}

void
KEY_Prep(struct req *req)
{
	printf("  KEY_Prep(req: %p)\n", req);
	printf("   - key_b %p\n", req->key_b);
	printf("   - key_l %p\n", req->key_l);
	printf("   - key_e %p\n", req->key_e);
	req->key_b = req->vary_b;
	req->key_l = req->vary_l;
	req->key_e = req->vary_e;
	printf("   - key_b %p\n", req->key_b);
	printf("   - key_l %p\n", req->key_l);
	printf("   - key_e %p\n", req->key_e);
	printf("  KEY_Prep(req: %p) = void\n", req);
}

void
KEY_Finish(struct req *req, struct busyobj *bo)
{
	printf("  KEY_Finish(req: %p, bo: %p)\n", req, bo);
	printf("   - key_b %p\n", req->key_b);
	printf("   - key_l %p\n", req->key_l);
	printf("   - key_e %p\n", req->key_e);
	if (bo != NULL) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		//KEY_Validate(req->key_b);
		if (req->key_l != NULL) {
			bo->key = WS_Copy(bo->ws,
			    req->key_b, req->key_l - req->key_b);
			AN(bo->key);
			//KEY_Validate(bo->key);
		} else
			bo->key = NULL;
	}
	//WS_Release(req->ws, 0);
	req->key_b = NULL;
	req->key_l = NULL;
	req->key_e = NULL;
	printf("   - key_b %p\n", req->key_b);
	printf("   - key_l %p\n", req->key_l);
	printf("   - key_e %p\n", req->key_e);
	printf("  KEY_Finish(req: %p, bo: %p) = void\n", req, bo);
}

int
KEY_Match(struct req *req, const uint8_t *key)
{
	printf("    KEY_Match()\n");
	return 0;
}
