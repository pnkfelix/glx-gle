/************************************************************************
 *                                                                      *
 * GLE - Graphics Layout Engine <http://www.gle-graphics.org/>          *
 *                                                                      *
 * Modified BSD License                                                 *
 *                                                                      *
 * Copyright (C) 2009 GLE.                                              *
 *                                                                      *
 * Redistribution and use in source and binary forms, with or without   *
 * modification, are permitted provided that the following conditions   *
 * are met:                                                             *
 *                                                                      *
 *    1. Redistributions of source code must retain the above copyright *
 * notice, this list of conditions and the following disclaimer.        *
 *                                                                      *
 *    2. Redistributions in binary form must reproduce the above        *
 * copyright notice, this list of conditions and the following          *
 * disclaimer in the documentation and/or other materials provided with *
 * the distribution.                                                    *
 *                                                                      *
 *    3. The name of the author may not be used to endorse or promote   *
 * products derived from this software without specific prior written   *
 * permission.                                                          *
 *                                                                      *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR   *
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED       *
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE   *
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY       *
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL   *
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE    *
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS        *
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER *
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN  *
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                        *
 *                                                                      *
 ************************************************************************/

char *un_quote();
const char *ns[3] = {"nothing", "number", "string"};
extern int gle_debug;

/*---------------------------------------------------------------------------*/

#include "all.h"
#include "mem_limits.h"
#include "token.h"
#include "gle-interface/gle-interface.h"
#include "glearray.h"
#include "polish.h"
#include "pass.h"
#include "var.h"
#include "sub.h"
#include "gprint.h"
#include "cutils.h"
#include "keyword.h"
#include "run.h"

/*---------------------------------------------------------------------------*/
/* bin = 10..29, binstr = 30..49, fn= 60...139, userfn=LOCAL_START_INDEX..nnn */
#define stack_bin(i,p) 	stack_op(pcode,stk,stkp,&nstk,i-10+(last_typ*20),p+curpri)
#define stack_fn(i) 	stack_op(pcode,stk,stkp,&nstk,i+60,10+curpri)
#define dbg if ((gle_debug & 4)>0)

// #define dbg

/*---------------------------------------------------------------------------*/
/* Input is token array, and pointer to current point, output is pcode */

void stack_op(GLEPcode& pcode, int stk[], int stkp[], int *nstk,  int i, int p);
bool valid_unquoted_string(const string& str);

GLEPolish::GLEPolish() : m_lang(), m_tokens(&m_lang, false) {
	m_vars = NULL;
}

GLEPolish::~GLEPolish() {
}

void GLEPolish::initTokenizer() {
	TokenizerLanguage* lang = m_tokens.get_language();
	lang->setSpaceTokens(" \t\r\n");
	lang->setLineCommentTokens("!");
	lang->setSingleCharTokens(",.:;[]{}()+-*/=<>|^%\\");
	lang->setDecimalDot('.');
	lang->addSubLanguages(1);
	lang->addLanguageElem(0, "<=");
	lang->addLanguageElem(0, ">=");
	lang->addLanguageElem(0, "<>");
	lang->addLanguageElem(0, "**");
	m_tokens.select_language(0);
}

void GLEPolish::get_params(GLEPcode& pcode, int np, int* plist, const string& name) throw(ParserError) {
	int nb_param = 0;
	if (!m_tokens.is_next_token(")")) {
		while (true) {
			if (nb_param >= np) {
				char err_str[100];
				sprintf(err_str, "': found >= %d, expected %d", nb_param+1, np);
				throw error(string("too many parameters in call to '")+name+err_str);
			}
			int vtype = *(plist + nb_param);
			polish(pcode, &vtype);
			int next_token = m_tokens.is_next_token_in(",)");
			if (next_token == -1) {
				throw error(string("expecting ',' or ')' in parameter list of function '")+name+"'");
			}
			nb_param++;
			if (next_token == ')') break;
		}
	}
	if (nb_param != np) {
		char err_str[100];
		sprintf(err_str, "': found %d, expected %d", nb_param, np);
		throw error(string("incorrect number of parameters in call to '")+name+err_str);
	}
}

void GLEPolish::polish(const char *expr, GLEPcode& pcode, int *rtype) throw(ParserError) {
	#ifdef DEBUG_POLISH
		gprint("==== Start of expression {%s} \n",expr);
	#endif
	m_tokens.set_string(expr);
	polish(pcode, rtype);
}

void GLEPolish::polish(GLEPcode& pcode, int *rtype) throw(ParserError) {
	GLESub* sub;
	string uc_token;
	int idx, ret, np, *plist, term_bracket = false;
	int curpri = 0;
	int nstk = 0, stk[50], stkp[50];   /* stack for operators */
	int unary = 1;                     /* binary or unary operation expected */
	/* last_typ, 1=number,2=string */
	bool isa_string = false;
	bool not_string = false;
	if (*rtype==1) not_string = true;
	if (*rtype>0) term_bracket = true;
	int last_typ = *rtype;
	pcode.addInt(PCODE_EXPR);   /* Expression follows */
	int savelen = pcode.size(); /* Used to set acutal length at end */
	pcode.addInt(0);	    /* Length of expression */
	while (true) {
		string token = m_tokens.try_next_token();
		int token_col = m_tokens.token_pos_col();
		int token_len = token.length();
		char first_char = token_len > 0 ? token[0] : ' ';
		// cout << "Token: '" << token << "'" << endl;
		// end of stream, or found ',' or ')'
		if (token_len == 0 || (token_len == 1 && (first_char == ',' || (first_char == ')' && curpri == 0)))) {
			if (token_len != 0) {
				m_tokens.pushback_token();
			}
			*rtype = last_typ;
			dbg gprint("Found END OF EXPRESSION \n");
			if (curpri != 0) {
				throw error("unexpected end of expression, missing closing ')'");
			}
			/* Pop everything off the stack */
			for (int i = nstk; i > 0; i--) {
				dbg gprint("Adding left over operators  I = %d  op=%d \n",i,stk[i]);
				pcode.addInt(stk[i]);
			}
			if (unary == 1) {
				throw error("constant, function, or unary operator expected");
			}
			pcode.setInt(savelen, pcode.size() - savelen - 1);
			#ifdef DEBUG_POLISH
				pcode.show(savelen);
			#endif
			return;
		}
		dbg gprint("First word token via (1=unary %d) cts {%s}\n ", unary, token.c_str());
		switch (unary) {
		case 1:  /* a unary operator, or function, or number or variable */
			if (is_float(token)) {
				dbg gprint("Found number {%s}\n",token.c_str());
				double value = atof(token.c_str());
				pcode.addDouble(value);
				if (last_typ == 2) {
					throw error(token_col, "expecting string, but found number");
				}
				last_typ = 1;
				unary = 2;
				break;
			}
			str_to_uppercase(token, uc_token);
			/* NOT a number, is it a built in function? */
			find_un((char*)uc_token.c_str(), &idx, &ret, &np, &plist);
			/* 1,2 = +,- */
			if (idx > 3 && m_tokens.is_next_token("(")) {
				//
				// it is a built in function
				//
				dbg gprint("Found built in function \n");
				get_params(pcode, np, plist, uc_token);
				if (last_typ == (3-ret)) {
					throw error(token_col, string("illegal function return type: ")+ns[ret]);
				}
				last_typ = ret;
				pcode.addFunction(idx+60);
				unary = 2;
				break;
			} else if (idx > 0 && idx <= 3) {
				stack_fn(idx);
				unary = 1;
				break;
			}
			/* Is it a user-defined function, identical code too above. */
			sub = sub_find((char*)uc_token.c_str());
			if (sub != NULL && m_tokens.is_next_token("(")) {
				//
				// it is a user defined function
				//
//				printf("User cts=%s  idx=%d ret=%d np=%d plist=%d\n",cts,idx,ret,np,plist);
				dbg gprint("Found user function \n");
				get_params(pcode, sub->getNbParam(), sub->getParamTypes(), uc_token);
				if (ret > 0 && ret < 3) last_typ = ret;
				pcode.addFunction(sub->getIndex()+LOCAL_START_INDEX);
				unary = 2;
				break;
			}
			/* Is it a 'known' variable */
			int v;
			var_find((char*)uc_token.c_str(), &v, &ret);
			if (v >= 0) {
				// cout << "found var: '" << uc_token << "' -> " << v << endl;
				last_typ = ret;
				if (ret == 2) pcode.addStrVar(v);
				else pcode.addVar(v);
				unary = 2;
				if (m_vars != NULL && m_vars->try_get(uc_token) == -1) {
					/* Add it to list of vars */
					m_vars->add_item(uc_token, v);
				}
				break;
			}
			/* Is it a string */
			if (first_char == '"' || first_char == '\'') {
				dbg gprint("Found string \n");
				last_typ = 2;
				string str_no_quote = token;
				str_remove_quote(str_no_quote);
				pcode.addString(str_no_quote);
				unary = 2;
				break;
			}
			if (first_char == '(' && token_len == 1) {
				curpri = curpri + 100;
				break;
			}
			if (first_char == ')' && token_len == 1) {
				throw error("constant, function, or unary operator expected");
			}
			if (m_tokens.is_next_token("(")) {
				throw error(token_col, string("call to undefined function '"+token+"'"));
			}
			/* must be unquoted string, unless a binary operator
			   was found, in which case it is an undelcared variable */
			if (not_string || str_var(token)) {
				/* name that includes '$' is also assumed to be a variable */
				dbg gprint("Found un-initialized variable {%s} /n",token.c_str());
				if (!var_valid_name(uc_token)) {
					throw error(token_col, "illegal variable name '"+uc_token+"'");
				}
				var_findadd((char*)uc_token.c_str(), &v, &ret);
				last_typ = ret;
				if (ret == 2) pcode.addStrVar(v);
				else pcode.addVar(v);
				not_string = true;
				unary = 2;
				if (m_vars != NULL && m_vars->try_get(uc_token) == -1) {
					/* Add it to list of vars */
					m_vars->add_item(uc_token, v);
				}
				break;
			}
			last_typ = 2;
			dbg printf("Unquoted string (%s) \n",token.c_str());
			pcode.addString(token);
			if (!valid_unquoted_string(token)) {
				throw error(token_col, "invalid unquoted string '"+token+"'");
			}
			isa_string = true;
			unary = 2;
			break;
		case 2: /* a binary operator, or space, or end of line */
			/* MIGHT (gives error with a$ = b$+c$) */
			if (first_char != '.') {
				if (isa_string) {
					throw error("left hand side contains unquoted string");
				}
				not_string = true;
			} else {
				not_string = false;
			}
			/* Binary operators, +,-,*,/,^,<,>,<=,>=,.and.,.or. */
			int priority = 0;
			if (token_len == 1) {
				switch (first_char) {
					case '+' : v = 1;  priority = 2; break;
					case '-' : v = 2;  priority = 2; break;
					case '*' : v = 3;  priority = 3; break;
					case '/' : v = 4;  priority = 3; break;
					case '%' : v = 14; priority = 3; break;
					case '^' : v = 5;  priority = 4; break;
					case '=' : v = 6;  priority = 1; break;
					case '&' : v = 12; priority = 1; break;
					case '|' : v = 13; priority = 1; break;
					case '<' : v = 7;  priority = 1; break;
					case '>' : v = 9;  priority = 1; break;
					case '.' : v = 15;  priority = 2; break;
					default  : v = 0;
				}
			} else {
				str_to_uppercase(token, uc_token);
				if (token == "<=") {
					v = 8; priority = 1;
				} else if (token == "<>") {
					v = 11; priority = 1;
				} else if (token == ">=") {
					v = 10; priority = 1;
				} else if (token == "**") {
					v = 5;  priority = 4;
				} else if (uc_token == "AND") {
					v = 12; priority = 1;
				} else if (uc_token == "OR") {
					v = 13; priority = 1;
				} else {
					v = 0;
				}
			}
			if (v > 0) {
				if (last_typ < 1 || last_typ > 3) {
					last_typ = 1;
				}
				dbg gprint("stack, i %d, type %d \n",v,last_typ);
				stack_bin(v, priority);
				dbg gprint("Found binary operator \n");
				unary = 1;
			} else if (first_char == ')' && token_len == 1) {
				if (curpri > 0) {
					curpri = curpri - 100;
					unary = 2;
					break;
				}
				if (!term_bracket) {
					throw error("too many closing ')', expecting binary operator");
				}
			} else {
				throw error(string("unknown binary operator '")+token+"'");
			}
		} // end switch
	} // end for
}

Tokenizer* GLEPolish::getTokens(const string& str) {
	m_tokens.set_string(str.c_str());
	return &m_tokens;
}

void GLEPolish::eval(const char *exp, double *x) throw(ParserError) {
	int rtype = 1, otyp = 0, cp = 0;
	GLEPcodeList pc_list;
	GLEPcode pcode(&pc_list);
	// cout << "eval '" << exp << "'" << endl;
	try {
		polish(exp, pcode, &rtype);
	} catch (ParserError err) {
		err.setParserString(exp);
		throw err;
	}
	GLEArrayImpl* stk = 0;
	::eval(stk, (int*)&pcode[0], &cp, x, NULL, &otyp);
}

void GLEPolish::internalEval(const char *exp, double *x) throw(ParserError) {
	int rtype = 1, otyp = 0, cp = 0;
	GLEPcodeList pc_list;
	GLEPcode pcode(&pc_list);
	polish(exp, pcode, &rtype);
	GLEArrayImpl* stk = 0;
	::eval(stk, (int*)&pcode[0], &cp, x, NULL, &otyp);
}

void GLEPolish::internalEvalString(const char* exp, string* str) throw(ParserError) {
   double oval;
	const char* ostr;
	int rtype = 2, otyp = 0, cp = 0;
	GLEPcodeList pc_list;
	GLEPcode pcode(&pc_list);
	polish(exp, pcode, &rtype);
	GLEArrayImpl* stk = 0;
	::eval(stk, (int*)&pcode[0], &cp, &oval, &ostr, &otyp);
	if (otyp == 1) {
		stringstream str_cnv;
		str_cnv << oval;
		*str = str_cnv.str();
	} else {
		*str = ostr;
	}
}

void GLEPolish::eval_string(const char *exp, string *str, bool allownum) throw(ParserError) {
	const char* ostr;
	double oval = 0.0;
	int rtype = allownum ? 0 : 2;
	int otyp = 0, cp = 0;
	GLEPcodeList pc_list;
	GLEPcode pcode(&pc_list);
	try {
		polish(exp, pcode, &rtype);
	} catch (ParserError err) {
		err.setParserString(exp);
		throw err;
	}
	GLEArrayImpl* stk = 0;
	::eval(stk, (int*)&pcode[0], &cp, &oval, &ostr, &otyp);
	if (otyp == 1) {
		if (allownum) {
			stringstream str_cnv;
			str_cnv << oval;
			*str = str_cnv.str();
		} else {
			throw error(string("expression does not evaluate to string '")+exp+"'");
		}
	} else {
		*str = ostr;
	}
}

bool valid_unquoted_string(const string& str) {
	if (str.length() == 0) {
		return false;
	} else {
		int ch = str[0];
		if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) {
			return false;
		} else {
			return true;
		}
	}
}

void stack_op(GLEPcode& pcode, int stk[], int stkp[], int *nstk,  int i, int p) {
	dbg gprint("Stack oper %d priority %d \n",i,p);
	while ((*nstk)>0 && p<=stkp[*nstk]) {
		dbg gprint("ADDING oper stack = %d  oper=%d \n",*nstk,stk[(*nstk)]);
		pcode.addInt(stk[(*nstk)--]);
	}
	stk[++(*nstk)] = i;
	stkp[*nstk] = p;
}

void polish(char *expr, GLEPcode& pcode, int *rtype) throw(ParserError) {
	GLEPolish* polish = get_global_polish();
	if (polish != NULL) {
		try {
			polish->polish(expr, pcode, rtype);
		} catch (ParserError err) {
			err.setParserString(expr);
			throw err;
		}
	}
}

void eval_pcode(GLEPcode& pcode, double* x) {
	int otyp = 0, cp = 0;
	GLEArrayImpl* stk = 0;
	::eval(stk, (int*)&pcode[0], &cp, x, NULL, &otyp);
}

void eval_pcode_str(GLEPcode& pcode, string& x) {
	const char* ostr;
	int otyp = 1, cp = 0;
	double value;
	GLEArrayImpl* stk = 0;
	::eval(stk, (int*)&pcode[0], &cp, &value, &ostr, &otyp);
	x = ostr;
}

void polish(char *expr, char *pcode, int *plen, int *rtype) throw(ParserError) {
	GLEPolish* polish = get_global_polish();
	if (polish != NULL) {
		GLEPcodeList pc_list;
		GLEPcode my_pcode(&pc_list);
		try {
			polish->polish(expr, my_pcode, rtype);
		} catch (ParserError err) {
			err.setParserString(expr);
			throw err;
		}
		*plen = my_pcode.size();
		memcpy(pcode,&my_pcode[0],my_pcode.size()*sizeof(int));
	}
}

void polish_eval(char *exp, double *x) throw(ParserError) {
	GLEPolish* polish = get_global_polish();
	if (polish != NULL) polish->eval(exp, x);
}

void polish_eval_string(const char *exp, string *str, bool allownum) throw(ParserError) {
	GLEPolish* polish = get_global_polish();
	if (polish != NULL) polish->eval_string(exp, str, allownum);
}

GLEPcode::GLEPcode(GLEPcodeList* list) {
	m_PCodeList = list;
}

void GLEPcode::addDoubleExpression(double val) {
	addInt(PCODE_EXPR);
	int savelen = size();
	addInt(0);
	addDouble(val);
	setInt(savelen, size() - savelen - 1);
}

void GLEPcode::addStringExpression(const char* val) {
	addInt(PCODE_EXPR);
	int savelen = size();
	addInt(0);
	addStringChar(val);
	setInt(savelen, size() - savelen - 1);
}

void GLEPcode::addDouble(double val) {
	union { double d ; int l[2]; short s[4]; } both;
	both.d = val;
	addInt(PCODE_DOUBLE);
	addInt(both.l[0]);
	addInt(both.l[1]);
}

void GLEPcode::addFunction(int idx) {
	addInt(idx);
}

void GLEPcode::addVar(int var) {
	addInt(PCODE_VAR);
	addInt(var);
}

void GLEPcode::addStrVar(int var) {
	addInt(PCODE_STRVAR);
	addInt(var);
}

void GLEPcode::addString(const string& str) {
	addInt(PCODE_STRING);
	addStringNoID(str);
}

void GLEPcode::addStringNoID(const string& str) {
	int slen = str.length() + 1;
	slen = ((slen + 3) & 0xfffc) / 4;
	int pos = size();
	for (int i = 0; i < slen; i++) {
		addInt(0);
	}
	char* str_target = (char*)&(*this)[pos];
	strcpy(str_target, str.c_str());
}

void GLEPcode::addStringChar(const char* str) {
	addInt(PCODE_STRING);
	addStringNoIDChar(str);
}

void GLEPcode::addStringNoIDChar(const char* str) {
	int slen = strlen(str) + 1;
	slen = ((slen + 3) & 0xfffc) / 4;
	int pos = size();
	for (int i = 0; i < slen; i++) {
		addInt(0);
	}
	char* str_target = (char*)&(*this)[pos];
	strcpy(str_target, str);
}

void GLEPcode::show() {
	show(0);
}

void GLEPcode::show(int start) {
	cout << "PCode:" << endl;
	union { double d ; int l[2]; short s[4]; } both;
	int size = getInt(start);
	int pos = start+1;
	while (pos <= start+size) {
		int varid = 0;
		int stpos = pos;
		int opcode = getInt(pos++);
		switch (opcode) {
			case PCODE_VAR:
				varid = getInt(pos++);
				cout << "VAR " << varid << " (" << stpos << ")" << endl;
				break;
			case PCODE_DOUBLE:
				both.l[0] = getInt(pos++);
				both.l[1] = getInt(pos++);
				cout << "DOUBLE " << both.d << endl;
				break;
			default:
				cout << "PCODE " << opcode << " (" << stpos << ")" << endl;

		}
	}
}

GLEPcodeIndexed::GLEPcodeIndexed(GLEPcodeList* list) : GLEPcode(list) {
}

GLEFunctionParserPcode::GLEFunctionParserPcode() : m_Pcode(&m_PcodeList) {
}

GLEFunctionParserPcode::~GLEFunctionParserPcode() {
}

void GLEFunctionParserPcode::polish(const char* fct, StringIntHash* vars) throw(ParserError) {
	GLEPolish* polish = get_global_polish();
	if (polish != NULL) {
		try {
			int rtype = 1;
			polish->setExprVars(vars);
			polish->polish(fct, m_Pcode, &rtype);
			polish->setExprVars(NULL);
		} catch (ParserError err) {
			err.setParserString(fct);
			throw err;
		}
	}
}

void GLEFunctionParserPcode::polishPos(const char* fct, int pos, StringIntHash* vars) throw(ParserError) {
	GLEPolish* polish = get_global_polish();
	if (polish != NULL) {
		try {
			int rtype = 1;
			polish->setExprVars(vars);
			polish->polish(fct, m_Pcode, &rtype);
			polish->setExprVars(NULL);
		} catch (ParserError err) {
			err.incColumn(pos-1);
			throw err;
		}
	}
}

void GLEFunctionParserPcode::polishX() throw(ParserError) {
	polish("x", NULL);
}

double GLEFunctionParserPcode::evalDouble() {
	double value;
	eval_pcode(m_Pcode, &value);
	return value;
}
