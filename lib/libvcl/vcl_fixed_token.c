/*
 * NB:  This file is machine generated, DO NOT EDIT!
 * instead, edit the Tcl script vcl_gen_fixed_token.tcl and run it by hand
 */

#include <stdio.h>
#include <ctype.h>
#include "vcl_priv.h"

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
			return (T_DECR);
		}
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
	case 'a':
		if (p[0] == 'a' && p[1] == 'c' && p[2] == 'l'
		     && !isvar(p[3])) {
			*q = p + 3;
			return (T_ACL);
		}
		return (0);
	case 'b':
		if (p[0] == 'b' && p[1] == 'a' && p[2] == 'c' && 
		    p[3] == 'k' && p[4] == 'e' && p[5] == 'n' && 
		    p[6] == 'd' && !isvar(p[7])) {
			*q = p + 7;
			return (T_BACKEND);
		}
		return (0);
	case 'c':
		if (p[0] == 'c' && p[1] == 'a' && p[2] == 'l' && 
		    p[3] == 'l' && !isvar(p[4])) {
			*q = p + 4;
			return (T_CALL);
		}
		return (0);
	case 'e':
		if (p[0] == 'e' && p[1] == 'r' && p[2] == 'r' && 
		    p[3] == 'o' && p[4] == 'r' && !isvar(p[5])) {
			*q = p + 5;
			return (T_ERROR);
		}
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
	case 'f':
		if (p[0] == 'f' && p[1] == 'u' && p[2] == 'n' && 
		    p[3] == 'c' && !isvar(p[4])) {
			*q = p + 4;
			return (T_FUNC);
		}
		if (p[0] == 'f' && p[1] == 'i' && p[2] == 'n' && 
		    p[3] == 'i' && p[4] == 's' && p[5] == 'h'
		     && !isvar(p[6])) {
			*q = p + 6;
			return (T_FINISH);
		}
		if (p[0] == 'f' && p[1] == 'e' && p[2] == 't' && 
		    p[3] == 'c' && p[4] == 'h' && !isvar(p[5])) {
			*q = p + 5;
			return (T_FETCH);
		}
		return (0);
	case 'i':
		if (p[0] == 'i' && p[1] == 'f' && !isvar(p[2])) {
			*q = p + 2;
			return (T_IF);
		}
		return (0);
	case 'n':
		if (p[0] == 'n' && p[1] == 'o' && p[2] == '_' && 
		    p[3] == 'n' && p[4] == 'e' && p[5] == 'w' && 
		    p[6] == '_' && p[7] == 'c' && p[8] == 'a' && 
		    p[9] == 'c' && p[10] == 'h' && p[11] == 'e'
		     && !isvar(p[12])) {
			*q = p + 12;
			return (T_NO_NEW_CACHE);
		}
		if (p[0] == 'n' && p[1] == 'o' && p[2] == '_' && 
		    p[3] == 'c' && p[4] == 'a' && p[5] == 'c' && 
		    p[6] == 'h' && p[7] == 'e' && !isvar(p[8])) {
			*q = p + 8;
			return (T_NO_CACHE);
		}
		return (0);
	case 'p':
		if (p[0] == 'p' && p[1] == 'r' && p[2] == 'o' && 
		    p[3] == 'c' && !isvar(p[4])) {
			*q = p + 4;
			return (T_PROC);
		}
		if (p[0] == 'p' && p[1] == 'a' && p[2] == 's' && 
		    p[3] == 's' && !isvar(p[4])) {
			*q = p + 4;
			return (T_PASS);
		}
		return (0);
	case 'r':
		if (p[0] == 'r' && p[1] == 'e' && p[2] == 'w' && 
		    p[3] == 'r' && p[4] == 'i' && p[5] == 't' && 
		    p[6] == 'e' && !isvar(p[7])) {
			*q = p + 7;
			return (T_REWRITE);
		}
		return (0);
	case 's':
		if (p[0] == 's' && p[1] == 'w' && p[2] == 'i' && 
		    p[3] == 't' && p[4] == 'c' && p[5] == 'h' && 
		    p[6] == '_' && p[7] == 'c' && p[8] == 'o' && 
		    p[9] == 'n' && p[10] == 'f' && p[11] == 'i' && 
		    p[12] == 'g' && !isvar(p[13])) {
			*q = p + 13;
			return (T_SWITCH_CONFIG);
		}
		if (p[0] == 's' && p[1] == 'u' && p[2] == 'b'
		     && !isvar(p[3])) {
			*q = p + 3;
			return (T_SUB);
		}
		if (p[0] == 's' && p[1] == 'e' && p[2] == 't'
		     && !isvar(p[3])) {
			*q = p + 3;
			return (T_SET);
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
	vcl_tnames[CSTR] = "CSTR";
	vcl_tnames[EOI] = "EOI";
	vcl_tnames[ID] = "ID";
	vcl_tnames[T_ACL] = "acl";
	vcl_tnames[T_BACKEND] = "backend";
	vcl_tnames[T_CALL] = "call";
	vcl_tnames[T_CAND] = "&&";
	vcl_tnames[T_COR] = "||";
	vcl_tnames[T_DEC] = "--";
	vcl_tnames[T_DECR] = "/=";
	vcl_tnames[T_DIV] = "/=";
	vcl_tnames[T_ELSE] = "else";
	vcl_tnames[T_ELSEIF] = "elseif";
	vcl_tnames[T_ELSIF] = "elsif";
	vcl_tnames[T_EQ] = "==";
	vcl_tnames[T_ERROR] = "error";
	vcl_tnames[T_FETCH] = "fetch";
	vcl_tnames[T_FINISH] = "finish";
	vcl_tnames[T_FUNC] = "func";
	vcl_tnames[T_GEQ] = ">=";
	vcl_tnames[T_IF] = "if";
	vcl_tnames[T_INC] = "++";
	vcl_tnames[T_INCR] = "+=";
	vcl_tnames[T_LEQ] = "<=";
	vcl_tnames[T_MUL] = "*=";
	vcl_tnames[T_NEQ] = "!=";
	vcl_tnames[T_NO_CACHE] = "no_cache";
	vcl_tnames[T_NO_NEW_CACHE] = "no_new_cache";
	vcl_tnames[T_PASS] = "pass";
	vcl_tnames[T_PROC] = "proc";
	vcl_tnames[T_REWRITE] = "rewrite";
	vcl_tnames[T_SET] = "set";
	vcl_tnames[T_SHL] = "<<";
	vcl_tnames[T_SHR] = ">>";
	vcl_tnames[T_SUB] = "sub";
	vcl_tnames[T_SWITCH_CONFIG] = "switch_config";
	vcl_tnames[VAR] = "VAR";
}

void
vcl_output_lang_h(FILE *f)
{
	fputs("/*\n", f);
	fputs(" * Stuff necessary to compile a VCL programs C code\n", f);
	fputs(" *\n", f);
	fputs(" * XXX: When this file is changed, lib/libvcl/vcl_gen_fixed_token.tcl\n", f);
	fputs(" * XXX: *MUST* be rerun.\n", f);
	fputs(" */\n", f);
	fputs("\n", f);
	fputs("/* XXX: This include is bad.  The VCL compiler shouldn't know about it. */\n", f);
	fputs("#include <sys/queue.h>\n", f);
	fputs("\n", f);
	fputs("struct vcl_ref {\n", f);
	fputs("	unsigned	line;\n", f);
	fputs("	unsigned	pos;\n", f);
	fputs("	unsigned	count;\n", f);
	fputs("	const char	*token;\n", f);
	fputs("};\n", f);
	fputs("\n", f);
	fputs("struct vcl_acl {\n", f);
	fputs("	unsigned	ip;\n", f);
	fputs("	unsigned	mask;\n", f);
	fputs("};\n", f);
	fputs("\n", f);
	fputs("#define VCA_RXBUFSIZE		1024\n", f);
	fputs("#define VCA_ADDRBUFSIZE		32\n", f);
	fputs("#define VCA_UNKNOWNHDR		10\n", f);
	fputs("\n", f);
	fputs("struct httphdr {\n", f);
	fputs("	const char		*req;\n", f);
	fputs("	const char		*url;\n", f);
	fputs("	const char		*proto;\n", f);
	fputs("#define HTTPH(a, b, c, d, e, f, g) const char *b;\n", f);
	fputs("/*\n", f);
	fputs(" * $Id$\n", f);
	fputs(" *\n", f);
	fputs(" * a	Http header name\n", f);
	fputs(" * b	session field name\n", f);
	fputs(" * c	PassThrough handling (0=remove, 1=pass)\n", f);
	fputs(" * d	unused\n", f);
	fputs(" * e	unused\n", f);
	fputs(" * f	unused\n", f);
	fputs(" * g	unused\n", f);
	fputs(" *\n", f);
	fputs(" *    a                         b                       c  d  e  f  g \n", f);
	fputs(" *--------------------------------------------------------------------\n", f);
	fputs(" */\n", f);
	fputs("HTTPH(\"Accept-Charset\",		H_Accept_Charset,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Accept-Encoding\",	H_Accept_Encoding,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Accept-Language\",	H_Accept_Language,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Accept\",			H_Accept,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Authorization\",		H_Authorization,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Connection\",		H_Connection,		1, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Expect\",			H_Expect,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"From\",			H_From,			0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Host\",			H_Host,			1, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"If-Match\",		H_If_Match,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"If-Modified-Since\",	H_If_Modified_Since,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"If-None-Match\",		H_If_None_Match,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"If-Range\",		H_If_Range,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"If-Unmodified-Since\",	H_If_Unmodifed_Since,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Keep-Alive\",		H_Keep_Alive,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Max-Forwards\",		H_Max_Forwards,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Proxy-Authorization\",	H_Proxy_Authorization,	0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Range\",			H_Range,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"Referer\",		H_Referer,		0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"TE\",			H_TE,			0, 0, 0, 0, 0)\n", f);
	fputs("HTTPH(\"User-Agent\",		H_User_Agent,		1, 0, 0, 0, 0)\n", f);
	fputs("#undef HTTPH\n", f);
	fputs("	const char		*uhdr[VCA_UNKNOWNHDR];\n", f);
	fputs("	unsigned		nuhdr;\n", f);
	fputs("};\n", f);
	fputs("\n", f);
	fputs("struct sess {\n", f);
	fputs("	int			fd;\n", f);
	fputs("\n", f);
	fputs("	/* formatted ascii client address */\n", f);
	fputs("	char			addr[VCA_ADDRBUFSIZE];\n", f);
	fputs("\n", f);
	fputs("	/* Receive buffer for HTTP header */\n", f);
	fputs("	char			rcv[VCA_RXBUFSIZE + 1];\n", f);
	fputs("	unsigned		rcv_len;\n", f);
	fputs("\n", f);
	fputs("	/* HTTP request info, points into rcv */\n", f);
	fputs("	struct httphdr		http;\n", f);
	fputs("\n", f);
	fputs("	enum {\n", f);
	fputs("		HND_Unclass,\n", f);
	fputs("		HND_Handle,\n", f);
	fputs("		HND_Pass,\n", f);
	fputs("		HND_Pipe\n", f);
	fputs("	}			handling;\n", f);
	fputs("\n", f);
	fputs("	char			done;\n", f);
	fputs("\n", f);
	fputs("	TAILQ_ENTRY(sess)	list;\n", f);
	fputs("\n", f);
	fputs("	struct backend		*backend;\n", f);
	fputs("	struct VCL_conf		*vcl;\n", f);
	fputs("\n", f);
	fputs("	/* Various internal stuff */\n", f);
	fputs("	struct event		*rd_e;\n", f);
	fputs("	struct sessmem		*mem;\n", f);
	fputs("};\n", f);
	fputs("\n", f);
	fputs("struct backend {\n", f);
	fputs("	const char	*hostname;\n", f);
	fputs("	const char	*portname;\n", f);
	fputs("	struct addrinfo	*addr;\n", f);
	fputs("	unsigned	ip;\n", f);
	fputs("	double		responsetime;\n", f);
	fputs("	double		timeout;\n", f);
	fputs("	double		bandwidth;\n", f);
	fputs("	int		down;\n", f);
	fputs("\n", f);
	fputs("	/* internal stuff */\n", f);
	fputs("	struct vbe	*vbe;\n", f);
	fputs("};\n", f);
	fputs("\n", f);
	fputs("#define VCL_FARGS	struct sess *sess\n", f);
	fputs("#define VCL_PASS_ARGS	sess\n", f);
	fputs("\n", f);
	fputs("void VCL_count(unsigned);\n", f);
	fputs("void VCL_no_cache();\n", f);
	fputs("void VCL_no_new_cache();\n", f);
	fputs("int ip_match(unsigned, struct vcl_acl *);\n", f);
	fputs("int string_match(const char *, const char *);\n", f);
	fputs("int VCL_rewrite(const char *, const char *);\n", f);
	fputs("int VCL_error(unsigned, const char *);\n", f);
	fputs("void VCL_pass(VCL_FARGS);\n", f);
	fputs("int VCL_fetch(void);\n", f);
	fputs("int VCL_switch_config(const char *);\n", f);
	fputs("\n", f);
	fputs("typedef void vcl_init_f(void);\n", f);
	fputs("typedef void vcl_func_f(VCL_FARGS);\n", f);
	fputs("\n", f);
	fputs("struct VCL_conf {\n", f);
	fputs("	unsigned	magic;\n", f);
	fputs("#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */\n", f);
	fputs("	vcl_init_f	*init_func;\n", f);
	fputs("	vcl_func_f	*main_func;\n", f);
	fputs("	struct backend	*default_backend;\n", f);
	fputs("	struct vcl_ref	*ref;\n", f);
	fputs("	unsigned	nref;\n", f);
	fputs("	unsigned	busy;\n", f);
	fputs("};\n", f);
}
