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
#include "vcc_priv.h"
#include "vsb.h"

unsigned
vcl_fixed_token(const char *p, const char **q)
{

	switch (p[0]) {
	case '!':
		if (p[0] == '!' && p[1] == '=') {
			*q = p + 2;
			return (T_NEQ);
		}
		if (p[0] == '!') {
			*q = p + 1;
			return ('!');
		}
		return (0);
	case '%':
		if (p[0] == '%') {
			*q = p + 1;
			return ('%');
		}
		return (0);
	case '&':
		if (p[0] == '&' && p[1] == '&') {
			*q = p + 2;
			return (T_CAND);
		}
		if (p[0] == '&') {
			*q = p + 1;
			return ('&');
		}
		return (0);
	case '(':
		if (p[0] == '(') {
			*q = p + 1;
			return ('(');
		}
		return (0);
	case ')':
		if (p[0] == ')') {
			*q = p + 1;
			return (')');
		}
		return (0);
	case '*':
		if (p[0] == '*' && p[1] == '=') {
			*q = p + 2;
			return (T_MUL);
		}
		if (p[0] == '*') {
			*q = p + 1;
			return ('*');
		}
		return (0);
	case '+':
		if (p[0] == '+' && p[1] == '=') {
			*q = p + 2;
			return (T_INCR);
		}
		if (p[0] == '+' && p[1] == '+') {
			*q = p + 2;
			return (T_INC);
		}
		if (p[0] == '+') {
			*q = p + 1;
			return ('+');
		}
		return (0);
	case ',':
		if (p[0] == ',') {
			*q = p + 1;
			return (',');
		}
		return (0);
	case '-':
		if (p[0] == '-' && p[1] == '=') {
			*q = p + 2;
			return (T_DECR);
		}
		if (p[0] == '-' && p[1] == '-') {
			*q = p + 2;
			return (T_DEC);
		}
		if (p[0] == '-') {
			*q = p + 1;
			return ('-');
		}
		return (0);
	case '.':
		if (p[0] == '.') {
			*q = p + 1;
			return ('.');
		}
		return (0);
	case '/':
		if (p[0] == '/' && p[1] == '=') {
			*q = p + 2;
			return (T_DIV);
		}
		if (p[0] == '/') {
			*q = p + 1;
			return ('/');
		}
		return (0);
	case ';':
		if (p[0] == ';') {
			*q = p + 1;
			return (';');
		}
		return (0);
	case '<':
		if (p[0] == '<' && p[1] == '=') {
			*q = p + 2;
			return (T_LEQ);
		}
		if (p[0] == '<' && p[1] == '<') {
			*q = p + 2;
			return (T_SHL);
		}
		if (p[0] == '<') {
			*q = p + 1;
			return ('<');
		}
		return (0);
	case '=':
		if (p[0] == '=' && p[1] == '=') {
			*q = p + 2;
			return (T_EQ);
		}
		if (p[0] == '=') {
			*q = p + 1;
			return ('=');
		}
		return (0);
	case '>':
		if (p[0] == '>' && p[1] == '>') {
			*q = p + 2;
			return (T_SHR);
		}
		if (p[0] == '>' && p[1] == '=') {
			*q = p + 2;
			return (T_GEQ);
		}
		if (p[0] == '>') {
			*q = p + 1;
			return ('>');
		}
		return (0);
	case 'e':
		if (p[0] == 'e' && p[1] == 'l' && p[2] == 's' && 
		    p[3] == 'i' && p[4] == 'f' && !isvar(p[5])) {
			*q = p + 5;
			return (T_ELSIF);
		}
		if (p[0] == 'e' && p[1] == 'l' && p[2] == 's' && 
		    p[3] == 'e' && p[4] == 'i' && p[5] == 'f'
		     && !isvar(p[6])) {
			*q = p + 6;
			return (T_ELSEIF);
		}
		if (p[0] == 'e' && p[1] == 'l' && p[2] == 's' && 
		    p[3] == 'e' && !isvar(p[4])) {
			*q = p + 4;
			return (T_ELSE);
		}
		return (0);
	case 'i':
		if (p[0] == 'i' && p[1] == 'n' && p[2] == 'c' && 
		    p[3] == 'l' && p[4] == 'u' && p[5] == 'd' && 
		    p[6] == 'e' && !isvar(p[7])) {
			*q = p + 7;
			return (T_INCLUDE);
		}
		if (p[0] == 'i' && p[1] == 'f' && !isvar(p[2])) {
			*q = p + 2;
			return (T_IF);
		}
		return (0);
	case '{':
		if (p[0] == '{') {
			*q = p + 1;
			return ('{');
		}
		return (0);
	case '|':
		if (p[0] == '|' && p[1] == '|') {
			*q = p + 2;
			return (T_COR);
		}
		if (p[0] == '|') {
			*q = p + 1;
			return ('|');
		}
		return (0);
	case '}':
		if (p[0] == '}') {
			*q = p + 1;
			return ('}');
		}
		return (0);
	case '~':
		if (p[0] == '~') {
			*q = p + 1;
			return ('~');
		}
		return (0);
	default:
		return (0);
	}
}

const char *vcl_tnames[256];

void
vcl_init_tnames(void)
{
	vcl_tnames['!'] = "'!'";
	vcl_tnames['%'] = "'%'";
	vcl_tnames['&'] = "'&'";
	vcl_tnames['('] = "'('";
	vcl_tnames[')'] = "')'";
	vcl_tnames['*'] = "'*'";
	vcl_tnames['+'] = "'+'";
	vcl_tnames[','] = "','";
	vcl_tnames['-'] = "'-'";
	vcl_tnames['.'] = "'.'";
	vcl_tnames['/'] = "'/'";
	vcl_tnames['<'] = "'<'";
	vcl_tnames['='] = "'='";
	vcl_tnames['>'] = "'>'";
	vcl_tnames['{'] = "'{'";
	vcl_tnames['}'] = "'}'";
	vcl_tnames['|'] = "'|'";
	vcl_tnames['~'] = "'~'";
	vcl_tnames[';'] = "';'";
	vcl_tnames[CNUM] = "CNUM";
	vcl_tnames[CSRC] = "CSRC";
	vcl_tnames[CSTR] = "CSTR";
	vcl_tnames[EOI] = "EOI";
	vcl_tnames[ID] = "ID";
	vcl_tnames[T_CAND] = "&&";
	vcl_tnames[T_COR] = "||";
	vcl_tnames[T_DEC] = "--";
	vcl_tnames[T_DECR] = "-=";
	vcl_tnames[T_DIV] = "/=";
	vcl_tnames[T_ELSE] = "else";
	vcl_tnames[T_ELSEIF] = "elseif";
	vcl_tnames[T_ELSIF] = "elsif";
	vcl_tnames[T_EQ] = "==";
	vcl_tnames[T_GEQ] = ">=";
	vcl_tnames[T_IF] = "if";
	vcl_tnames[T_INC] = "++";
	vcl_tnames[T_INCLUDE] = "include";
	vcl_tnames[T_INCR] = "+=";
	vcl_tnames[T_LEQ] = "<=";
	vcl_tnames[T_MUL] = "*=";
	vcl_tnames[T_NEQ] = "!=";
	vcl_tnames[T_SHL] = "<<";
	vcl_tnames[T_SHR] = ">>";
	vcl_tnames[VAR] = "VAR";
}

void
vcl_output_lang_h(struct vsb *sb)
{
	vsb_cat(sb, "#define VCL_RET_ERROR  (1 << 0)\n");
	vsb_cat(sb, "#define VCL_RET_LOOKUP  (1 << 1)\n");
	vsb_cat(sb, "#define VCL_RET_HASH  (1 << 2)\n");
	vsb_cat(sb, "#define VCL_RET_PIPE  (1 << 3)\n");
	vsb_cat(sb, "#define VCL_RET_PASS  (1 << 4)\n");
	vsb_cat(sb, "#define VCL_RET_FETCH  (1 << 5)\n");
	vsb_cat(sb, "#define VCL_RET_INSERT  (1 << 6)\n");
	vsb_cat(sb, "#define VCL_RET_DELIVER  (1 << 7)\n");
	vsb_cat(sb, "#define VCL_RET_DISCARD  (1 << 8)\n");
	vsb_cat(sb, "#define VCL_RET_KEEP  (1 << 9)\n");
	vsb_cat(sb, "#define VCL_RET_RESTART  (1 << 10)\n");
	vsb_cat(sb, "/*\n");
	vsb_cat(sb, " * $Id$\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * NB:  This file is machine generated, DO NOT EDIT!\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * Edit vcc_gen_fixed_token.tcl instead\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct sess;\n");
	vsb_cat(sb, "struct cli;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "typedef void vcl_init_f(struct cli *);\n");
	vsb_cat(sb, "typedef void vcl_fini_f(struct cli *);\n");
	vsb_cat(sb, "typedef int vcl_func_f(struct sess *sp);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct VCL_conf {\n");
	vsb_cat(sb, "	unsigned        magic;\n");
	vsb_cat(sb, "#define VCL_CONF_MAGIC  0x7406c509      /* from /dev/random */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "        struct director  **director;\n");
	vsb_cat(sb, "        unsigned        ndirector;\n");
	vsb_cat(sb, "        struct vrt_ref  *ref;\n");
	vsb_cat(sb, "        unsigned        nref;\n");
	vsb_cat(sb, "        unsigned        busy;\n");
	vsb_cat(sb, "        unsigned        discard;\n");
	vsb_cat(sb, "        \n");
	vsb_cat(sb, "	unsigned	nsrc;\n");
	vsb_cat(sb, "	const char	**srcname;\n");
	vsb_cat(sb, "	const char	**srcbody;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "	unsigned	nhashcount;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "        void            *priv;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "        vcl_init_f      *init_func;\n");
	vsb_cat(sb, "        vcl_fini_f      *fini_func;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "	vcl_func_f	*recv_func;\n");
	vsb_cat(sb, "	vcl_func_f	*pipe_func;\n");
	vsb_cat(sb, "	vcl_func_f	*pass_func;\n");
	vsb_cat(sb, "	vcl_func_f	*hash_func;\n");
	vsb_cat(sb, "	vcl_func_f	*miss_func;\n");
	vsb_cat(sb, "	vcl_func_f	*hit_func;\n");
	vsb_cat(sb, "	vcl_func_f	*fetch_func;\n");
	vsb_cat(sb, "	vcl_func_f	*deliver_func;\n");
	vsb_cat(sb, "	vcl_func_f	*prefetch_func;\n");
	vsb_cat(sb, "	vcl_func_f	*timeout_func;\n");
	vsb_cat(sb, "	vcl_func_f	*discard_func;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "/*-\n");
	vsb_cat(sb, " * Copyright (c) 2006 Verdens Gang AS\n");
	vsb_cat(sb, " * Copyright (c) 2006-2008 Linpro AS\n");
	vsb_cat(sb, " * All rights reserved.\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * Redistribution and use in source and binary forms, with or without\n");
	vsb_cat(sb, " * modification, are permitted provided that the following conditions\n");
	vsb_cat(sb, " * are met:\n");
	vsb_cat(sb, " * 1. Redistributions of source code must retain the above copyright\n");
	vsb_cat(sb, " *    notice, this list of conditions and the following disclaimer.\n");
	vsb_cat(sb, " * 2. Redistributions in binary form must reproduce the above copyright\n");
	vsb_cat(sb, " *    notice, this list of conditions and the following disclaimer in the\n");
	vsb_cat(sb, " *    documentation and/or other materials provided with the distribution.\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND\n");
	vsb_cat(sb, " * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n");
	vsb_cat(sb, " * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n");
	vsb_cat(sb, " * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE\n");
	vsb_cat(sb, " * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n");
	vsb_cat(sb, " * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS\n");
	vsb_cat(sb, " * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)\n");
	vsb_cat(sb, " * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT\n");
	vsb_cat(sb, " * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY\n");
	vsb_cat(sb, " * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF\n");
	vsb_cat(sb, " * SUCH DAMAGE.\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * $Id$\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * Runtime support for compiled VCL programs.\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * XXX: When this file is changed, lib/libvcl/vcc_gen_fixed_token.tcl\n");
	vsb_cat(sb, " * XXX: *MUST* be rerun.\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct sess;\n");
	vsb_cat(sb, "struct vsb;\n");
	vsb_cat(sb, "struct cli;\n");
	vsb_cat(sb, "struct director;\n");
	vsb_cat(sb, "struct VCL_conf;\n");
	vsb_cat(sb, "struct sockaddr;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/*\n");
	vsb_cat(sb, " * A backend probe specification\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct vrt_backend_probe {\n");
	vsb_cat(sb, "	char		*request;\n");
	vsb_cat(sb, "	double		timeout;\n");
	vsb_cat(sb, "	double		interval;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/*\n");
	vsb_cat(sb, " * A backend is a host+port somewhere on the network\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "struct vrt_backend {\n");
	vsb_cat(sb, "	char				*vcl_name;\n");
	vsb_cat(sb, "	char				*ident;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "	char				*hosthdr;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "	const unsigned char		*ipv4_sockaddr;\n");
	vsb_cat(sb, "	const unsigned char		*ipv6_sockaddr;\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "	double				connect_timeout;\n");
	vsb_cat(sb, "	struct vrt_backend_probe 	probe;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/*\n");
	vsb_cat(sb, " * A director with a predictable reply\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct vrt_dir_simple {\n");
	vsb_cat(sb, "	const char				*name;\n");
	vsb_cat(sb, "	const struct vrt_backend		*host;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/*\n");
	vsb_cat(sb, " * A director with an unpredictable reply\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct vrt_dir_random_entry {\n");
	vsb_cat(sb, "	const struct vrt_backend		*host;\n");
	vsb_cat(sb, "	double					weight;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct vrt_dir_random {\n");
	vsb_cat(sb, "	const char 				*name;\n");
	vsb_cat(sb, "	unsigned 				nmember;\n");
	vsb_cat(sb, "	const struct vrt_dir_random_entry	*members;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/*\n");
	vsb_cat(sb, " * other stuff.\n");
	vsb_cat(sb, " * XXX: document when bored\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct vrt_ref {\n");
	vsb_cat(sb, "	unsigned	source;\n");
	vsb_cat(sb, "	unsigned	offset;\n");
	vsb_cat(sb, "	unsigned	line;\n");
	vsb_cat(sb, "	unsigned	pos;\n");
	vsb_cat(sb, "	unsigned	count;\n");
	vsb_cat(sb, "	const char	*token;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct vrt_acl {\n");
	vsb_cat(sb, "	unsigned char	not;\n");
	vsb_cat(sb, "	unsigned char	mask;\n");
	vsb_cat(sb, "	unsigned char	paren;\n");
	vsb_cat(sb, "	const char	*name;\n");
	vsb_cat(sb, "	const char	*desc;\n");
	vsb_cat(sb, "	void		*priv;\n");
	vsb_cat(sb, "};\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/* ACL related */\n");
	vsb_cat(sb, "int VRT_acl_match(const struct sess *, struct sockaddr *, const char *, const struct vrt_acl *);\n");
	vsb_cat(sb, "void VRT_acl_init(struct vrt_acl *);\n");
	vsb_cat(sb, "void VRT_acl_fini(struct vrt_acl *);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/* Regexp related */\n");
	vsb_cat(sb, "void VRT_re_init(void **, const char *, int sub);\n");
	vsb_cat(sb, "void VRT_re_fini(void *);\n");
	vsb_cat(sb, "int VRT_re_match(const char *, void *re);\n");
	vsb_cat(sb, "int VRT_re_test(struct vsb *, const char *, int sub);\n");
	vsb_cat(sb, "const char *VRT_regsub(const struct sess *sp, int all, const char *, void *, const char *);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "void VRT_purge(const char *, int hash);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "void VRT_count(const struct sess *, unsigned);\n");
	vsb_cat(sb, "int VRT_rewrite(const char *, const char *);\n");
	vsb_cat(sb, "void VRT_error(struct sess *, unsigned, const char *);\n");
	vsb_cat(sb, "int VRT_switch_config(const char *);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "enum gethdr_e { HDR_REQ, HDR_RESP, HDR_OBJ, HDR_BEREQ };\n");
	vsb_cat(sb, "char *VRT_GetHdr(const struct sess *, enum gethdr_e where, const char *);\n");
	vsb_cat(sb, "void VRT_SetHdr(const struct sess *, enum gethdr_e where, const char *, const char *, ...);\n");
	vsb_cat(sb, "void VRT_handling(struct sess *sp, unsigned hand);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/* Simple stuff */\n");
	vsb_cat(sb, "int VRT_strcmp(const char *s1, const char *s2);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "void VRT_ESI(struct sess *sp);\n");
	vsb_cat(sb, "void VRT_Rollback(struct sess *sp);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "/* Backend related */\n");
	vsb_cat(sb, "void VRT_init_dir_simple(struct cli *, struct director **, const struct vrt_dir_simple *);\n");
	vsb_cat(sb, "void VRT_init_dir_random(struct cli *, struct director **, const struct vrt_dir_random *);\n");
	vsb_cat(sb, "void VRT_fini_dir(struct cli *, struct director *);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "char *VRT_IP_string(const struct sess *sp, const struct sockaddr *sa);\n");
	vsb_cat(sb, "char *VRT_int_string(const struct sess *sp, int);\n");
	vsb_cat(sb, "char *VRT_double_string(const struct sess *sp, double);\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "#define VRT_done(sp, hand)			\\\n");
	vsb_cat(sb, "	do {					\\\n");
	vsb_cat(sb, "		VRT_handling(sp, hand);		\\\n");
	vsb_cat(sb, "		return (1);			\\\n");
	vsb_cat(sb, "	} while (0)\n");
	vsb_cat(sb, "/*\n");
	vsb_cat(sb, " * $Id$\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * NB:  This file is machine generated, DO NOT EDIT!\n");
	vsb_cat(sb, " *\n");
	vsb_cat(sb, " * Edit vcc_gen_obj.tcl instead\n");
	vsb_cat(sb, " */\n");
	vsb_cat(sb, "\n");
	vsb_cat(sb, "struct sockaddr * VRT_r_client_ip(const struct sess *);\n");
	vsb_cat(sb, "struct sockaddr * VRT_r_server_ip(struct sess *);\n");
	vsb_cat(sb, "const char * VRT_r_req_request(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_req_request(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "const char * VRT_r_req_url(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_req_url(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "const char * VRT_r_req_proto(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_req_proto(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "void VRT_l_req_hash(struct sess *, const char *);\n");
	vsb_cat(sb, "struct director * VRT_r_req_backend(struct sess *);\n");
	vsb_cat(sb, "void VRT_l_req_backend(struct sess *, struct director *);\n");
	vsb_cat(sb, "int VRT_r_req_restarts(const struct sess *);\n");
	vsb_cat(sb, "double VRT_r_req_grace(struct sess *);\n");
	vsb_cat(sb, "void VRT_l_req_grace(struct sess *, double);\n");
	vsb_cat(sb, "const char * VRT_r_bereq_request(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_bereq_request(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "const char * VRT_r_bereq_url(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_bereq_url(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "const char * VRT_r_bereq_proto(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_bereq_proto(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "const char * VRT_r_obj_proto(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_proto(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "int VRT_r_obj_status(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_status(const struct sess *, int);\n");
	vsb_cat(sb, "const char * VRT_r_obj_response(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_response(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "unsigned VRT_r_obj_valid(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_valid(const struct sess *, unsigned);\n");
	vsb_cat(sb, "unsigned VRT_r_obj_cacheable(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_cacheable(const struct sess *, unsigned);\n");
	vsb_cat(sb, "double VRT_r_obj_ttl(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_ttl(const struct sess *, double);\n");
	vsb_cat(sb, "double VRT_r_obj_grace(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_grace(const struct sess *, double);\n");
	vsb_cat(sb, "double VRT_r_obj_prefetch(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_obj_prefetch(const struct sess *, double);\n");
	vsb_cat(sb, "double VRT_r_obj_lastuse(const struct sess *);\n");
	vsb_cat(sb, "const char * VRT_r_obj_hash(const struct sess *);\n");
	vsb_cat(sb, "const char * VRT_r_resp_proto(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_resp_proto(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "int VRT_r_resp_status(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_resp_status(const struct sess *, int);\n");
	vsb_cat(sb, "const char * VRT_r_resp_response(const struct sess *);\n");
	vsb_cat(sb, "void VRT_l_resp_response(const struct sess *, const char *, ...);\n");
	vsb_cat(sb, "double VRT_r_now(const struct sess *);\n");
	vsb_cat(sb, "int VRT_r_backend_health(const struct sess *);\n");
}
