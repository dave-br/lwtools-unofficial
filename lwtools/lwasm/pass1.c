/*
pass1.c

Copyright © 2010 William Astle

This file is part of LWTOOLS.

LWTOOLS is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <lw_alloc.h>
#include <lw_string.h>

#include "lwasm.h"
#include "instab.h"
#include "input.h"

int expand_macro(asmstate_t *as, line_t *l, char **p, char *opc);
int expand_struct(asmstate_t *as, line_t *l, char **p, char *opc);
int add_macro_line(asmstate_t *as, char *optr);

/*
pass 1: parse the lines

line format if PRAGMA_NEWSOURCE is not in force:

[<symbol>] <opcode> <operand>[ <comment>]

If <symbol> is followed by a :, whitespace may precede the symbol

A line may optionally start with a number which must not be preceded by
white space and must be followed by a single whitespace character. After
that whitespace character, the line is parsed as if it had no line number.

Also, no spaces are permitted within <operand>.

With PRAGMA_NEWSOURCE in effect, line numbers are not allowed and there
is no automatic comment at the end of each line. All comments must be
introduced with the comment character. This allows the parser to handle
spaces in operands unambiguously so in this mode, spaces are permitted
within operands.

*/
void do_pass1(asmstate_t *as)
{
	char *line;
	line_t *cl;
	char *p1;
	int stspace;
	char *tok, *sym = NULL;
	int opnum;
	int lc = 1;
	int nomacro;
	int wasmacro;
	for (;;)
	{
		nomacro = 0;
		if (sym)
			lw_free(sym);
		sym = NULL;
		line = input_readline(as);
		if (!line)
			break;
		if (line[0] == 1 && line[1] == 1)
		{
			// special internal directive
			// these DO NOT appear in the output anywhere
			// they are generated by the parser to pass information
			// forward
			for (p1 = line + 2; *p1 && !isspace(*p1); p1++)
				/* do nothing */ ;
			*p1++ = '\0';
			if (!strcmp(line + 2, "SETCONTEXT"))
			{
				as -> context = strtol(p1, NULL, 10);
			}
			else if (!strcmp(line + 2, "SETLINENO"))
			{
				lc = strtol(p1, NULL, 10);
			}
			else if (!strcmp(line + 2, "SETNOEXPANDSTART"))
			{
				as -> line_tail -> noexpand_start += 1;
			}
			else if (!strcmp(line + 2, "SETNOEXPANDEND"))
			{
				as -> line_tail -> noexpand_end += 1;
			}
			lw_free(line);
			if (lc == 0)
				lc = 1;
			continue;
		}
		debug_message(as, 75, "Read line: %s", line);
		
		wasmacro = as -> inmacro;
		cl = lw_alloc(sizeof(line_t));
		memset(cl, 0, sizeof(line_t));
		cl -> outputl = -1;
		cl -> linespec = lw_strdup(input_curspec(as));
		cl -> file_path = (const char *) lw_stack_top(as -> full_paths);
		cl -> prev = as -> line_tail;
		cl -> insn = -1;
		cl -> as = as;
		cl -> inmod = as -> inmod;
		cl -> csect = as -> csect;
		cl -> pragmas = as -> pragmas;
		cl -> context = as -> context;
		cl -> ltext = lw_strdup(line);
		cl -> soff = -1;
		cl -> dshow = -1;
		cl -> dsize = 0;
		cl -> dptr = NULL;
		cl -> isbrpt = 0;
		cl -> dlen = 0;
		as -> cl = cl;
		if (!as -> line_tail)
		{
			as -> line_head = cl;
			cl -> addr = lw_expr_build(lw_expr_type_int, 0);
			cl -> daddr = lw_expr_build(lw_expr_type_int, 0);
		}
		else
		{
			lw_expr_t te;

			cl -> lineno = as -> line_tail -> lineno + 1;
			as -> line_tail -> next = cl;

			// set the line address
			te = lw_expr_build(lw_expr_type_special, lwasm_expr_linelen, cl -> prev);
			cl -> addr = lw_expr_build(lw_expr_type_oper, lw_expr_oper_plus, cl -> prev -> addr, te);
			lw_expr_destroy(te);
			lwasm_reduce_expr(as, cl -> addr);
			
			if (cl -> prev -> phase)
			{
				te = lw_expr_build(lw_expr_type_special, lwasm_expr_linelen, cl -> prev);
				cl -> phase = lw_expr_build(lw_expr_type_oper, lw_expr_oper_plus, cl -> prev -> phase, te);
				lw_expr_destroy(te);
				lwasm_reduce_expr(as, cl -> phase);
			}
//			lw_expr_simplify(cl -> addr, as);

			// set the data address if relevant
			if (as -> output_format == OUTPUT_OS9)
			{
				te = lw_expr_build(lw_expr_type_special, lwasm_expr_linedlen, cl -> prev);
				cl -> daddr = lw_expr_build(lw_expr_type_oper, lw_expr_oper_plus, cl -> prev -> daddr, te);
				lw_expr_destroy(te);
				lwasm_reduce_expr(as, cl -> daddr);
			}
			else
			{
				cl -> daddr = lw_expr_copy(cl -> addr);
			}

			// carry DP value forward
			cl -> dpval = cl -> prev -> dpval;
			
		}
		debug_message(as, 100, "Line pointer: %p", cl);
		if (!lc && strcmp(cl -> linespec, cl -> prev -> linespec))
			lc = 1;
		if (lc)
		{
			cl -> lineno = lc;
			lc = 0;
		}
		as -> line_tail = cl;
		// blank lines don't count for anything
		// except a local symbol context break
		if (!*line)
		{
			as -> context = lwasm_next_context(as);
			goto nextline;
		}
	
		// skip comments
		// comments do not create a context break
		if (*line == '*' || *line == ';' || *line == '#')
			goto nextline;

		p1 = line;
		if (isdigit(*p1) && !CURPRAGMA(cl, PRAGMA_NEWSOURCE))
		{
			// skip line number
			while (*p1 && isdigit(*p1))
				p1++;
			if (!*p1 && !isspace(*p1))
				p1 = line;
			else if (*p1 && !isspace(*p1))
				p1 = line;
			else if (*p1 && isspace(*p1))
				p1++;
		}

		// blank line - context break
		if (!*p1)
		{
			as -> context = lwasm_next_context(as);
			goto nextline;
		}

		// comment - no context break
		if (*p1 == '*' || *p1 == ';' || *p1 == '#')
			goto nextline;

		if (isspace(*p1))
		{
			for (; *p1 && isspace(*p1); p1++)
				/* do nothing */ ;
			stspace = 1;
		}
		else
			stspace = 0;

		if (!*p1)
		{
			// nothing but whitespace - context break
			as -> context = lwasm_next_context(as);
			goto nextline;
		}

		// find the end of the first token
		for (tok = p1; *p1 && !isspace(*p1) && *p1 != ':' && *p1 != '='; p1++)
			/* do nothing */ ;
		
		if (*p1 == ':' || *p1 == '=' || stspace == 0)
		{
			if (*tok == '*' || *tok == ';' || *tok == '#')
				goto nextline;
			// have a symbol here
			sym = lw_strndup(tok, p1 - tok);
			if (*p1 == ':')
				p1++;
			for (; *p1 && isspace(*p1); p1++)
				/* do nothing */ ;
		
			if (*p1 == '=')
			{
				tok = p1++;
			}
			else
			{
				for (tok = p1; *p1 && !isspace(*p1); p1++)
					/* do nothing */ ;
			}
		}
		if (sym && strcmp(sym, "!") == 0)
			cl -> isbrpt = 1;
		else if (sym)
			cl -> sym = lw_strdup(sym);
		cl -> symset = 0;
		
		// tok points to the opcode for the line or NUL if none
		
		// if the first two chars of the opcode are "??", that's
		// a flag to inhibit macro expansion
		if (*tok && tok[0] == '?' && tok[1] == '?')
		{
			nomacro = 1;
			tok += 2;
		}
		if (*tok)
		{
			if (CURPRAGMA(cl, PRAGMA_TESTMODE))
			{
				/* in test mode, terminate the line here so we don't affect the parsers */
				/* (cl -> ltext retains the full, unmodified string) */
				char *t = strstr(p1, ";.");
				if (t) *t = 0;
			}

			// look up operation code
			lw_free(sym);
			sym = lw_strndup(tok, p1 - tok);
			for (; *p1 && isspace(*p1); p1++)
				/* do nothing */ ;

			for (opnum = 0; instab[opnum].opcode; opnum++)
			{
				// ignore 6800 compatibility opcodes unless asked for
				if ((instab[opnum].flags & lwasm_insn_is6800) && !CURPRAGMA(cl, PRAGMA_6800COMPAT)) continue;
				// ignore 6809 convenience opcodes unless asked for
				if ((instab[opnum].flags & lwasm_insn_is6809conv) && !CURPRAGMA(cl, PRAGMA_6809CONV)) continue;
				// ignore 6809 convenience opcodes in 6309 mode
				if ((instab[opnum].flags & lwasm_insn_is6809conv) && !CURPRAGMA(cl, PRAGMA_6809)) continue;
				// ignore 6309 convenience opcodes unless asked for
				if ((instab[opnum].flags & lwasm_insn_is6309conv) && !CURPRAGMA(cl, PRAGMA_6309CONV)) continue;
				// ignore emulator extension opcodes unless asked for
				if ((instab[opnum].flags & lwasm_insn_isemuext) && !CURPRAGMA(cl, PRAGMA_EMUEXT)) continue;

				if (!strcasecmp(instab[opnum].opcode, sym))
					break;
			}
			
			// have to go to linedone here in case there was a symbol
			// to register on this line
			if (instab[opnum].opcode == NULL && (*tok == '*' || *tok == ';' || *tok == '#'))
				goto linedone;
			
			// p1 points to the start of the operand
			
			// if we're inside a macro definition and not at ENDM,
			// add the line to the macro definition and continue
			if (as -> inmacro && !(instab[opnum].flags & lwasm_insn_endm))
			{
				goto linedone;
			}
			
			// if skipping a condition and the operation code doesn't
			// operate within a condition (not a conditional)
			// do nothing
			if (as -> skipcond && !(instab[opnum].flags & lwasm_insn_cond))
				goto linedone;
        	
			if (!nomacro && (as->pragmas & PRAGMA_SHADOW))
        	{
        		// check for macros even if they shadow real operations
        		// NOTE: "ENDM" cannot be shadowed
        		if (expand_macro(as, cl, &p1, sym) == 0)
        		{
        			// a macro was expanded here
        			goto linedone;
        		}
        	}
        	
			if (instab[opnum].opcode == NULL ||
				(CURPRAGMA(cl, PRAGMA_6809) && (instab[opnum].flags & lwasm_insn_is6309)) ||
				(!CURPRAGMA(cl, PRAGMA_6809) && (instab[opnum].flags & lwasm_insn_is6809))
			)
			{
				cl -> insn = -1;
				if (*tok != ';' && *tok != '*')
				{
					// bad opcode; check for macro here
					// but don't expand it if "nomacro" is in effect
					if (nomacro || expand_macro(as, cl, &p1, sym) != 0)
					{
						// macro expansion failed
						if (expand_struct(as, cl, &p1, sym) != 0)
						{
							// structure expansion failed
							if (CURPRAGMA(cl, PRAGMA_6809) && (instab[opnum].flags & lwasm_insn_is6309))
								lwasm_register_error2(as, cl, E_6309_INVALID, "(%s)", sym);
							else if (!CURPRAGMA(cl, PRAGMA_6809) && (instab[opnum].flags & lwasm_insn_is6809))
								lwasm_register_error2(as, cl, E_6809_INVALID, "(%s)", sym);
							else
								lwasm_register_error(as, cl, E_OPCODE_BAD);
						}
					}
				}
			}
			else
			{
				cl -> insn = opnum;
				// no parse func means operand doesn't matter
				if (instab[opnum].parse)
				{
					if (CURPRAGMA(cl, PRAGMA_6809) && (instab[opnum].flags & lwasm_insn_is6309))
						lwasm_register_error2(as, cl, E_6309_INVALID, "(%s)", sym);
					if (!CURPRAGMA(cl, PRAGMA_6809) && (instab[opnum].flags & lwasm_insn_is6809))
						lwasm_register_error2(as, cl, E_6809_INVALID, "(%s)", sym);

					if (as -> instruct == 0 || instab[opnum].flags & lwasm_insn_struct)
					{
						cl -> len = -1;
						// call parse function
						debug_message(as, 100, "len = %d, dlen = %d", cl -> len, cl -> dlen);
						(instab[opnum].parse)(as, cl, &p1);

						// if we're forcing address modes on pass 1, force a resolution
						if (CURPRAGMA(cl, PRAGMA_FORWARDREFMAX) && instab[opnum].resolve)
						{
							(instab[opnum].resolve)(as, cl, 1);
						}
						if ((cl -> inmod == 0) && cl -> len >= 0 && cl -> dlen >= 0)
						{
							if (cl -> len == 0)
								cl -> len = cl -> dlen;
							else
								cl -> dlen = cl -> len;
						}
						if (!CURPRAGMA(cl, PRAGMA_NEWSOURCE))
						{
							if (*p1 && !isspace(*p1) && !(cl -> err))
							{
								// flag bad operand error
								lwasm_register_error2(as, cl, E_OPERAND_BAD, "(%s)", p1);
							}
						}
						else
						{
							lwasm_skip_to_next_token(cl, &p1);
							/* if we did not hit the end of the line and we aren't at a comment character, error out */
							if (*p1 && *p1 != ';' && *p1 != '#' && *p1 != ';')
							{
								// flag bad operand error
								lwasm_register_error2(as, cl, E_OPERAND_BAD, "%s", p1);
							}
						}
						
						/* do a reduction on the line expressions to avoid carrying excessive expression baggage if not needed */
						lwasm_reduce_line_exprs(cl);
					}
					else if (as -> instruct == 1)
					{
						lwasm_register_error2(as, cl, E_OPERAND_BAD, "(%s)", p1);
					}
				}
			}
		}
	
	linedone:
		if (as -> inmacro && wasmacro)
			add_macro_line(as, line);
		if (!as -> skipcond && !as -> inmacro)
		{
			if (cl -> sym && cl -> symset == 0)
			{
				// register symbol at line address
				if ((cl -> insn >= 0) && (instab[cl -> insn].flags & lwasm_insn_setdata))
				{
					debug_message(as, 50, "Register symbol %s: %s (D)", cl -> sym, lw_expr_print(cl -> daddr));
					if (!register_symbol(as, cl, cl -> sym, cl -> daddr, symbol_flag_constant))
					{
						// symbol error
						// lwasm_register_error2(as, cl, E_SYMBOL_BAD, "(%s)", cl -> sym);
					}
				}
				else
				{
					debug_message(as, 50, "Register symbol %s: %s", cl -> sym, lw_expr_print(cl -> phase ? cl -> phase : cl -> addr));
					if (!register_symbol(as, cl, cl -> sym, cl -> phase ? cl -> phase : cl -> addr, symbol_flag_none))
					{
						// symbol error
						// lwasm_register_error2(as, cl, E_SYMBOL_BAD, "(%s)", cl -> sym);
					}
				}
			}
			debug_message(as, 40, "Line address: %s", lw_expr_print(cl -> addr));
		}

nextline:
		if (as -> skipcond || as -> inmacro || cl -> ltext[0] == 1)
			cl -> hideline = 1;
		if (as -> skipcond)
			cl -> hidecond = 1;
		
//	nextline:
		if (sym)
			lw_free(sym);
		sym = NULL;
		
		lw_free(line);
		
		if (as -> preprocess && cl -> hideline == 0)
		{
			printf("%s\n", cl -> ltext);
		}
		
		// if we've hit the "end" bit, finish out
		if (as -> endseen)
			return;
	}
}
