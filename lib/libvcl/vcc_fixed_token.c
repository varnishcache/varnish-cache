/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_fixed_token.tcl instead
 */

#include "config.h"
#include <stdio.h>
#include <ctype.h>
#include "config.h"
#include "vcc_priv.h"
#include "vsb.h"

#define M1()     do {*q = p + 1; return (p[0]); } while (0)
#define M2(c, t) do {if (p[1] == (c)) { *q = p + 2; return (t); }} while (0)

unsigned
vcl_fixed_token(const char *p, const char **q)
{

	switch (p[0]) {
	case '!':
		M2('=', T_NEQ);
		M1();
	case '%':
		M1();
	case '&':
		M2('&', T_CAND);
		M1();
	case '(':
		M1();
	case ')':
		M1();
	case '*':
		M2('=', T_MUL);
		M1();
	case '+':
		M2('=', T_INCR);
		M2('+', T_INC);
		M1();
	case ',':
		M1();
	case '-':
		M2('=', T_DECR);
		M2('-', T_DEC);
		M1();
	case '.':
		M1();
	case '/':
		M2('=', T_DIV);
		M1();
	case ';':
		M1();
	case '<':
		M2('=', T_LEQ);
		M2('<', T_SHL);
		M1();
	case '=':
		M2('=', T_EQ);
		M1();
	case '>':
		M2('>', T_SHR);
		M2('=', T_GEQ);
		M1();
	case 'e':
		if (p[1] == 'l' && p[2] == 's' && p[3] == 'i' &&
		    p[4] == 'f' && !isvar(p[5])) {
			*q = p + 5;
			return (T_ELSIF);
		}
		if (p[1] == 'l' && p[2] == 's' && p[3] == 'e' &&
		    p[4] == 'i' && p[5] == 'f' && !isvar(p[6])) {
			*q = p + 6;
			return (T_ELSEIF);
		}
		if (p[1] == 'l' && p[2] == 's' && p[3] == 'e'
		     && !isvar(p[4])) {
			*q = p + 4;
			return (T_ELSE);
		}
		return (0);
	case 'i':
		if (p[1] == 'n' && p[2] == 'c' && p[3] == 'l' &&
		    p[4] == 'u' && p[5] == 'd' && p[6] == 'e'
		     && !isvar(p[7])) {
			*q = p + 7;
			return (T_INCLUDE);
		}
		M2('f', T_IF);
		return (0);
	case '{':
		M1();
	case '|':
		M2('|', T_COR);
		M1();
	case '}':
		M1();
	case '~':
		M1();
	default:
		return (0);
	}
}

const char *vcl_tnames[256] = {
	['!'] = "'!'",
	['%'] = "'%'",
	['&'] = "'&'",
	['('] = "'('",
	[')'] = "')'",
	['*'] = "'*'",
	['+'] = "'+'",
	[','] = "','",
	['-'] = "'-'",
	['.'] = "'.'",
	['/'] = "'/'",
	['<'] = "'<'",
	['='] = "'='",
	['>'] = "'>'",
	['{'] = "'{'",
	['}'] = "'}'",
	['|'] = "'|'",
	['~'] = "'~'",
	[';'] = "';'",
	[CNUM] = "CNUM",
	[CSRC] = "CSRC",
	[CSTR] = "CSTR",
	[EOI] = "EOI",
	[ID] = "ID",
	[T_CAND] = "&&",
	[T_COR] = "||",
	[T_DEC] = "--",
	[T_DECR] = "-=",
	[T_DIV] = "/=",
	[T_ELSE] = "else",
	[T_ELSEIF] = "elseif",
	[T_ELSIF] = "elsif",
	[T_EQ] = "==",
	[T_GEQ] = ">=",
	[T_IF] = "if",
	[T_INC] = "++",
	[T_INCLUDE] = "include",
	[T_INCR] = "+=",
	[T_LEQ] = "<=",
	[T_MUL] = "*=",
	[T_NEQ] = "!=",
	[T_SHL] = "<<",
	[T_SHR] = ">>",
	[VAR] = "VAR",
};

void
vcl_output_lang_h(struct vsb *sb)
{
	vsb_cat(sb, "#define VCL_RET_ERROR  (1 << 0)\n");
	vsb_cat(sb, "#define VCL_RET_LOOKUP  (1 << 1)\n");
	vsb_cat(sb, "#define VCL_RET_HASH  (1 << 2)\n");
	vsb_cat(sb, "#define VCL_RET_PIPE  (1 << 3)\n");
	vsb_cat(sb, "#define VCL_RET_PASS  (1 << 4)\n");
	vsb_cat(sb, "#define VCL_RET_FETCH  (1 << 5)\n");
	vsb_cat(sb, "#define VCL_RET_DELIVER  (1 << 6)\n");
	vsb_cat(sb, "#define VCL_RET_DISCARD  (1 << 7)\n");
	vsb_cat(sb, "#define VCL_RET_KEEP  (1 << 8)\n");
	vsb_cat(sb, "#define VCL_RET_RESTART  (1 << 9)\n");

	/* ../../include/vcl.h */

	vsb_cat(sb, "/*\n * $Id: vcc_gen_fixed_token.tcl 3098 2008-08-18 08");
	vsb_cat(sb, ":18:43Z phk $\n *\n * NB:  This file is machine genera");
	vsb_cat(sb, "ted, DO NOT EDIT!\n *\n * Edit vcc_gen_fixed_token.tcl");
	vsb_cat(sb, " instead\n */\n\nstruct sess;\nstruct cli;\n\ntypedef ");
	vsb_cat(sb, "void vcl_init_f(struct cli *);\ntypedef void vcl_fini_");
	vsb_cat(sb, "f(struct cli *);\ntypedef int vcl_func_f(struct sess *");
	vsb_cat(sb, "sp);\n\nstruct VCL_conf {\n\tunsigned        magic;\n#");
	vsb_cat(sb, "define VCL_CONF_MAGIC  0x7406c509      /* from /dev/ra");
	vsb_cat(sb, "ndom */\n\n        struct director  **director;\n     ");
	vsb_cat(sb, "   unsigned        ndirector;\n        struct vrt_ref ");
	vsb_cat(sb, " *ref;\n        unsigned        nref;\n        unsigne");
	vsb_cat(sb, "d        busy;\n        unsigned        discard;\n\n\t");
	vsb_cat(sb, "unsigned\tnsrc;\n\tconst char\t**srcname;\n\tconst cha");
	vsb_cat(sb, "r\t**srcbody;\n\n\tunsigned\tnhashcount;\n\n        vc");
	vsb_cat(sb, "l_init_f      *init_func;\n        vcl_fini_f      *fi");
	vsb_cat(sb, "ni_func;\n\n\tvcl_func_f\t*recv_func;\n\tvcl_func_f\t*");
	vsb_cat(sb, "pipe_func;\n\tvcl_func_f\t*pass_func;\n\tvcl_func_f\t*");
	vsb_cat(sb, "hash_func;\n\tvcl_func_f\t*miss_func;\n\tvcl_func_f\t*");
	vsb_cat(sb, "hit_func;\n\tvcl_func_f\t*fetch_func;\n\tvcl_func_f\t*");
	vsb_cat(sb, "deliver_func;\n\tvcl_func_f\t*prefetch_func;\n\tvcl_fu");
	vsb_cat(sb, "nc_f\t*timeout_func;\n\tvcl_func_f\t*discard_func;\n\t");
	vsb_cat(sb, "vcl_func_f\t*error_func;\n};\n");

	/* ../../include/vrt.h */

	vsb_cat(sb, "/*-\n * Copyright (c) 2006 Verdens Gang AS\n * Copyrig");
	vsb_cat(sb, "ht (c) 2006-2008 Linpro AS\n * All rights reserved.\n ");
	vsb_cat(sb, "*\n * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>\n");
	vsb_cat(sb, " *\n * Redistribution and use in source and binary for");
	vsb_cat(sb, "ms, with or without\n * modification, are permitted pr");
	vsb_cat(sb, "ovided that the following conditions\n * are met:\n * ");
	vsb_cat(sb, "1. Redistributions of source code must retain the abov");
	vsb_cat(sb, "e copyright\n *    notice, this list of conditions and");
	vsb_cat(sb, " the following disclaimer.\n * 2. Redistributions in b");
	vsb_cat(sb, "inary form must reproduce the above copyright\n *    n");
	vsb_cat(sb, "otice, this list of conditions and the following discl");
	vsb_cat(sb, "aimer in the\n *    documentation and/or other materia");
	vsb_cat(sb, "ls provided with the distribution.\n *\n * THIS SOFTWA");
	vsb_cat(sb, "RE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'");
	vsb_cat(sb, "' AND\n * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING");
	vsb_cat(sb, ", BUT NOT LIMITED TO, THE\n * IMPLIED WARRANTIES OF ME");
	vsb_cat(sb, "RCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n *");
	vsb_cat(sb, " ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBU");
	vsb_cat(sb, "TORS BE LIABLE\n * FOR ANY DIRECT, INDIRECT, INCIDENTA");
	vsb_cat(sb, "L, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n * DAMAGES (I");
	vsb_cat(sb, "NCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUT");
	vsb_cat(sb, "E GOODS\n * OR SERVICES; LOSS OF USE, DATA, OR PROFITS");
	vsb_cat(sb, "; OR BUSINESS INTERRUPTION)\n * HOWEVER CAUSED AND ON ");
	vsb_cat(sb, "ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT\n");
	vsb_cat(sb, " * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWI");
	vsb_cat(sb, "SE) ARISING IN ANY WAY\n * OUT OF THE USE OF THIS SOFT");
	vsb_cat(sb, "WARE, EVEN IF ADVISED OF THE POSSIBILITY OF\n * SUCH D");
	vsb_cat(sb, "AMAGE.\n *\n * $Id: vrt.h 3234 2008-09-29 07:32:26Z ph");
	vsb_cat(sb, "k $\n *\n * Runtime support for compiled VCL programs.");
	vsb_cat(sb, "\n *\n * XXX: When this file is changed, lib/libvcl/vc");
	vsb_cat(sb, "c_gen_fixed_token.tcl\n * XXX: *MUST* be rerun.\n */\n");
	vsb_cat(sb, "\nstruct sess;\nstruct vsb;\nstruct cli;\nstruct direc");
	vsb_cat(sb, "tor;\nstruct VCL_conf;\nstruct sockaddr;\n\n/*\n * A b");
	vsb_cat(sb, "ackend probe specification\n */\n\nextern void *vrt_ma");
	vsb_cat(sb, "gic_string_end;\n\nstruct vrt_backend_probe {\n\tconst");
	vsb_cat(sb, " char\t*url;\n\tconst char\t*request;\n\tdouble\t\ttim");
	vsb_cat(sb, "eout;\n\tdouble\t\tinterval;\n\tunsigned\twindow;\n\tu");
	vsb_cat(sb, "nsigned\tthreshold;\n};\n\n/*\n * A backend is a host+");
	vsb_cat(sb, "port somewhere on the network\n */\nstruct vrt_backend");
	vsb_cat(sb, " {\n\tconst char\t\t\t*vcl_name;\n\tconst char\t\t\t*i");
	vsb_cat(sb, "dent;\n\n\tconst char\t\t\t*hosthdr;\n\n\tconst unsign");
	vsb_cat(sb, "ed char\t\t*ipv4_sockaddr;\n\tconst unsigned char\t\t*");
	vsb_cat(sb, "ipv6_sockaddr;\n\n\tdouble\t\t\t\tconnect_timeout;\n\t");
	vsb_cat(sb, "unsigned\t\t\tmax_connections;\n\tstruct vrt_backend_p");
	vsb_cat(sb, "robe\tprobe;\n};\n\n/*\n * A director with a predictab");
	vsb_cat(sb, "le reply\n */\n\nstruct vrt_dir_simple {\n\tconst char");
	vsb_cat(sb, "\t\t\t\t*name;\n\tconst struct vrt_backend\t\t*host;\n");
	vsb_cat(sb, "};\n\n/*\n * A director with an unpredictable reply\n ");
	vsb_cat(sb, "*/\n\nstruct vrt_dir_random_entry {\n\tconst struct vr");
	vsb_cat(sb, "t_backend\t\t*host;\n\tdouble\t\t\t\t\tweight;\n};\n\n");
	vsb_cat(sb, "struct vrt_dir_random {\n\tconst char\t\t\t\t*name;\n\t");
	vsb_cat(sb, "unsigned\t\t\t\tretries;\n\tunsigned\t\t\t\tnmember;\n");
	vsb_cat(sb, "\tconst struct vrt_dir_random_entry\t*members;\n};\n\n");
	vsb_cat(sb, "/*\n * A director with round robin selection\n */\n\ns");
	vsb_cat(sb, "truct vrt_dir_round_robin_entry {\n\tconst struct vrt_");
	vsb_cat(sb, "backend\t\t*host;\n};\n\nstruct vrt_dir_round_robin {\n");
	vsb_cat(sb, "\tconst char\t\t\t\t*name;\n\tunsigned\t\t\t\tnmember;");
	vsb_cat(sb, "\n\tconst struct vrt_dir_round_robin_entry\t*members;\n");
	vsb_cat(sb, "};\n\n\n/*\n * other stuff.\n * XXX: document when bor");
	vsb_cat(sb, "ed\n */\n\nstruct vrt_ref {\n\tunsigned\tsource;\n\tun");
	vsb_cat(sb, "signed\toffset;\n\tunsigned\tline;\n\tunsigned\tpos;\n");
	vsb_cat(sb, "\tunsigned\tcount;\n\tconst char\t*token;\n};\n\n/* AC");
	vsb_cat(sb, "L related */\n#define VRT_ACL_MAXADDR\t\t16\t/* max(IP");
	vsb_cat(sb, "v4, IPv6) */\n\nvoid VRT_acl_log(const struct sess *, ");
	vsb_cat(sb, "const char *msg);\n\n/* Regexp related */\nvoid VRT_re");
	vsb_cat(sb, "_init(void **, const char *, int sub);\nvoid VRT_re_fi");
	vsb_cat(sb, "ni(void *);\nint VRT_re_match(const char *, void *re);");
	vsb_cat(sb, "\nint VRT_re_test(struct vsb *, const char *, int sub)");
	vsb_cat(sb, ";\nconst char *VRT_regsub(const struct sess *sp, int a");
	vsb_cat(sb, "ll, const char *,\n    void *, const char *);\n\nvoid ");
	vsb_cat(sb, "VRT_panic(struct sess *sp,  const char *, ...);\nvoid ");
	vsb_cat(sb, "VRT_purge(const char *, int hash);\n\nvoid VRT_count(c");
	vsb_cat(sb, "onst struct sess *, unsigned);\nint VRT_rewrite(const ");
	vsb_cat(sb, "char *, const char *);\nvoid VRT_error(struct sess *, ");
	vsb_cat(sb, "unsigned, const char *);\nint VRT_switch_config(const ");
	vsb_cat(sb, "char *);\n\nenum gethdr_e { HDR_REQ, HDR_RESP, HDR_OBJ");
	vsb_cat(sb, ", HDR_BEREQ };\nchar *VRT_GetHdr(const struct sess *, ");
	vsb_cat(sb, "enum gethdr_e where, const char *);\nvoid VRT_SetHdr(c");
	vsb_cat(sb, "onst struct sess *, enum gethdr_e where, const char *,");
	vsb_cat(sb, "\n    const char *, ...);\nvoid VRT_handling(struct se");
	vsb_cat(sb, "ss *sp, unsigned hand);\n\n/* Simple stuff */\nint VRT");
	vsb_cat(sb, "_strcmp(const char *s1, const char *s2);\nvoid VRT_mem");
	vsb_cat(sb, "move(void *dst, const void *src, unsigned len);\n\nvoi");
	vsb_cat(sb, "d VRT_ESI(struct sess *sp);\nvoid VRT_Rollback(struct ");
	vsb_cat(sb, "sess *sp);\n\n/* Synthetic pages */\nvoid VRT_synth_pa");
	vsb_cat(sb, "ge(struct sess *sp, unsigned flags, const char *, ...)");
	vsb_cat(sb, ";\n\n/* Backend related */\nvoid VRT_init_dir_simple(s");
	vsb_cat(sb, "truct cli *, struct director **,\n    const struct vrt");
	vsb_cat(sb, "_dir_simple *);\nvoid VRT_init_dir_random(struct cli *");
	vsb_cat(sb, ", struct director **,\n    const struct vrt_dir_random");
	vsb_cat(sb, " *);\nvoid VRT_init_dir_round_robin(struct cli *, stru");
	vsb_cat(sb, "ct director **,\n    const struct vrt_dir_round_robin ");
	vsb_cat(sb, "*);\nvoid VRT_fini_dir(struct cli *, struct director *");
	vsb_cat(sb, ");\n\nchar *VRT_IP_string(const struct sess *sp, const");
	vsb_cat(sb, " struct sockaddr *sa);\nchar *VRT_int_string(const str");
	vsb_cat(sb, "uct sess *sp, int);\nchar *VRT_double_string(const str");
	vsb_cat(sb, "uct sess *sp, double);\nconst char *VRT_backend_string");
	vsb_cat(sb, "(struct sess *sp);\n\n#define VRT_done(sp, hand)\t\t\t");
	vsb_cat(sb, "\\\n\tdo {\t\t\t\t\t\\\n\t\tVRT_handling(sp, hand);\t\t");
	vsb_cat(sb, "\\\n\t\treturn (1);\t\t\t\\\n\t} while (0)\n");

	/* ../../include/vrt_obj.h */

	vsb_cat(sb, "/*\n * $Id: vcc_gen_obj.tcl 3169 2008-09-08 09:49:01Z ");
	vsb_cat(sb, "tfheen $\n *\n * NB:  This file is machine generated, ");
	vsb_cat(sb, "DO NOT EDIT!\n *\n * Edit vcc_gen_obj.tcl instead\n */");
	vsb_cat(sb, "\n\nstruct sockaddr * VRT_r_client_ip(const struct ses");
	vsb_cat(sb, "s *);\nstruct sockaddr * VRT_r_server_ip(struct sess *");
	vsb_cat(sb, ");\nint VRT_r_server_port(struct sess *);\nconst char ");
	vsb_cat(sb, "* VRT_r_req_request(const struct sess *);\nvoid VRT_l_");
	vsb_cat(sb, "req_request(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "const char * VRT_r_req_url(const struct sess *);\nvoid");
	vsb_cat(sb, " VRT_l_req_url(const struct sess *, const char *, ...)");
	vsb_cat(sb, ";\nconst char * VRT_r_req_proto(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_req_proto(const struct sess *, const char *");
	vsb_cat(sb, ", ...);\nvoid VRT_l_req_hash(struct sess *, const char");
	vsb_cat(sb, " *);\nstruct director * VRT_r_req_backend(struct sess ");
	vsb_cat(sb, "*);\nvoid VRT_l_req_backend(struct sess *, struct dire");
	vsb_cat(sb, "ctor *);\nint VRT_r_req_restarts(const struct sess *);");
	vsb_cat(sb, "\ndouble VRT_r_req_grace(struct sess *);\nvoid VRT_l_r");
	vsb_cat(sb, "eq_grace(struct sess *, double);\nconst char * VRT_r_r");
	vsb_cat(sb, "eq_xid(struct sess *);\nconst char * VRT_r_bereq_reque");
	vsb_cat(sb, "st(const struct sess *);\nvoid VRT_l_bereq_request(con");
	vsb_cat(sb, "st struct sess *, const char *, ...);\nconst char * VR");
	vsb_cat(sb, "T_r_bereq_url(const struct sess *);\nvoid VRT_l_bereq_");
	vsb_cat(sb, "url(const struct sess *, const char *, ...);\nconst ch");
	vsb_cat(sb, "ar * VRT_r_bereq_proto(const struct sess *);\nvoid VRT");
	vsb_cat(sb, "_l_bereq_proto(const struct sess *, const char *, ...)");
	vsb_cat(sb, ";\nconst char * VRT_r_obj_proto(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_proto(const struct sess *, const char *");
	vsb_cat(sb, ", ...);\nint VRT_r_obj_status(const struct sess *);\nv");
	vsb_cat(sb, "oid VRT_l_obj_status(const struct sess *, int);\nconst");
	vsb_cat(sb, " char * VRT_r_obj_response(const struct sess *);\nvoid");
	vsb_cat(sb, " VRT_l_obj_response(const struct sess *, const char *,");
	vsb_cat(sb, " ...);\nint VRT_r_obj_hits(const struct sess *);\nunsi");
	vsb_cat(sb, "gned VRT_r_obj_cacheable(const struct sess *);\nvoid V");
	vsb_cat(sb, "RT_l_obj_cacheable(const struct sess *, unsigned);\ndo");
	vsb_cat(sb, "uble VRT_r_obj_ttl(const struct sess *);\nvoid VRT_l_o");
	vsb_cat(sb, "bj_ttl(const struct sess *, double);\ndouble VRT_r_obj");
	vsb_cat(sb, "_grace(const struct sess *);\nvoid VRT_l_obj_grace(con");
	vsb_cat(sb, "st struct sess *, double);\ndouble VRT_r_obj_prefetch(");
	vsb_cat(sb, "const struct sess *);\nvoid VRT_l_obj_prefetch(const s");
	vsb_cat(sb, "truct sess *, double);\ndouble VRT_r_obj_lastuse(const");
	vsb_cat(sb, " struct sess *);\nconst char * VRT_r_obj_hash(const st");
	vsb_cat(sb, "ruct sess *);\nconst char * VRT_r_resp_proto(const str");
	vsb_cat(sb, "uct sess *);\nvoid VRT_l_resp_proto(const struct sess ");
	vsb_cat(sb, "*, const char *, ...);\nint VRT_r_resp_status(const st");
	vsb_cat(sb, "ruct sess *);\nvoid VRT_l_resp_status(const struct ses");
	vsb_cat(sb, "s *, int);\nconst char * VRT_r_resp_response(const str");
	vsb_cat(sb, "uct sess *);\nvoid VRT_l_resp_response(const struct se");
	vsb_cat(sb, "ss *, const char *, ...);\ndouble VRT_r_now(const stru");
	vsb_cat(sb, "ct sess *);\nunsigned VRT_r_req_backend_healthy(const ");
	vsb_cat(sb, "struct sess *);\n");
}
