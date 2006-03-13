/*
 * NB:  This file is machine generated, DO NOT EDIT!
 * instead, edit the Tcl script vcl_gen_fixed_token.tcl and run it by hand
 */

#include "vcl_priv.h"

unsigned
fixed_token(const char *p, const char **q)
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

const char *tnames[256] = {
	['!'] = "'!'" /* t2 '!' T! */,
	['%'] = "'%'" /* t2 '%' T% */,
	['&'] = "'&'" /* t2 '&' T& */,
	['('] = "'('" /* t2 '(' T( */,
	[')'] = "')'" /* t2 ')' T) */,
	['*'] = "'*'" /* t2 '*' T* */,
	['+'] = "'+'" /* t2 '+' T+ */,
	[','] = "','" /* t2 ',' T, */,
	['-'] = "'-'" /* t2 '-' T- */,
	['.'] = "'.'" /* t2 '.' T. */,
	['/'] = "'/'" /* t2 '/' T/ */,
	['<'] = "'<'" /* t2 '<' T< */,
	['='] = "'='" /* t2 '=' T= */,
	['>'] = "'>'" /* t2 '>' T> */,
	['{'] = "'{'" /* t2 '\{' T\{ */,
	['}'] = "'}'" /* t2 '\}' T\} */,
	['|'] = "'|'" /* t2 '|' T| */,
	['~'] = "'~'" /* t2 '~' T~ */,
	[';'] = "';'" /* t2 {';'} {T;} */,
	[CNUM] = "CNUM" /* t CNUM CNUM */,
	[CSTR] = "CSTR" /* t CSTR CSTR */,
	[EOI] = "EOI" /* t EOI EOI */,
	[ID] = "ID" /* t ID ID */,
	[T_ACL] = "acl" /* t T_ACL acl */,
	[T_BACKEND] = "backend" /* t T_BACKEND backend */,
	[T_CALL] = "call" /* t T_CALL call */,
	[T_CAND] = "&&" /* t T_CAND && */,
	[T_COR] = "||" /* t T_COR || */,
	[T_DEC] = "--" /* t T_DEC -- */,
	[T_DECR] = "/=" /* t T_DECR /= */,
	[T_DIV] = "/=" /* t T_DIV /= */,
	[T_ELSE] = "else" /* t T_ELSE else */,
	[T_ELSEIF] = "elseif" /* t T_ELSEIF elseif */,
	[T_ELSIF] = "elsif" /* t T_ELSIF elsif */,
	[T_EQ] = "==" /* t T_EQ == */,
	[T_ERROR] = "error" /* t T_ERROR error */,
	[T_FETCH] = "fetch" /* t T_FETCH fetch */,
	[T_FINISH] = "finish" /* t T_FINISH finish */,
	[T_FUNC] = "func" /* t T_FUNC func */,
	[T_GEQ] = ">=" /* t T_GEQ >= */,
	[T_IF] = "if" /* t T_IF if */,
	[T_INC] = "++" /* t T_INC ++ */,
	[T_INCR] = "+=" /* t T_INCR += */,
	[T_LEQ] = "<=" /* t T_LEQ <= */,
	[T_MUL] = "*=" /* t T_MUL *= */,
	[T_NEQ] = "!=" /* t T_NEQ != */,
	[T_NO_CACHE] = "no_cache" /* t T_NO_CACHE no_cache */,
	[T_NO_NEW_CACHE] = "no_new_cache" /* t T_NO_NEW_CACHE no_new_cache */,
	[T_PROC] = "proc" /* t T_PROC proc */,
	[T_REWRITE] = "rewrite" /* t T_REWRITE rewrite */,
	[T_SET] = "set" /* t T_SET set */,
	[T_SHL] = "<<" /* t T_SHL << */,
	[T_SHR] = ">>" /* t T_SHR >> */,
	[T_SUB] = "sub" /* t T_SUB sub */,
	[T_SWITCH_CONFIG] = "switch_config" /* t T_SWITCH_CONFIG switch_config */,
	[VAR] = "VAR" /* t VAR VAR */,
};
