/*
symdump.c

Copyright © 2019 William Astle

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
#include <stdlib.h>
#include <string.h>

#include <lw_alloc.h>
#include <lw_expr.h>
#include <lw_string.h>

#include "lwasm.h"
#include <srcdbg_api.h>

struct listinfo
{
	sectiontab_t *sect;
	asmstate_t *as;
	int complex;
};

int dump_symbols_test(lw_expr_t e, void *p)
{
	struct listinfo *li = p;
	
	if (li -> complex)
		return 0;
	
	if (lw_expr_istype(e, lw_expr_type_special))
	{
		if (lw_expr_specint(e) == lwasm_expr_secbase)
		{
			if (li -> sect)
			{
				li -> complex = 1;
			}
			else
			{
				li -> sect = lw_expr_specptr(e);
			}
		}
	}
	return 0;
}

void dump_symbols_aux(asmstate_t *as, FILE *of, void * mdi_simp_state, int apply_section_filter, sectiontab_t *csect, struct symtabe *se)
{
	struct symtabe *s;
	lw_expr_t te;
	struct listinfo li;
	int mame_err;

	li.as = as;
	
	if (!se)
		return;
	
	dump_symbols_aux(as, of, mdi_simp_state, apply_section_filter, csect, se -> left);
	
	for (s = se; s; s = s -> nextver)
	{	
		if (s -> flags & symbol_flag_nolist)
			continue;

		if (s -> context >= 0)
			continue;

		lwasm_reduce_expr(as, s -> value);

		te = lw_expr_copy(s -> value);
		li.complex = 0;
		li.sect = NULL;
		lw_expr_testterms(te, dump_symbols_test, &li);
		if (li.sect)
		{
			as -> exportcheck = 1;
			as -> csect = li.sect;
			lwasm_reduce_expr(as, te);
			as -> exportcheck = 0;
		}

		if (of)		
		{
			fprintf(of, "%s ", s -> symbol);
			if (s -> flags & symbol_flag_set)
				fputs("SET", of);
			else
				fputs("EQU", of);
			if (lw_expr_istype(te, lw_expr_type_int))
			{
				fprintf(of, " $%04X\n", lw_expr_intval(te));
			}
			else
			{
				fprintf(of, " 0 ; <<incomplete>>\n");
			}
		}
		else if (lw_expr_istype(te, lw_expr_type_int) &&
			// Apply section filter if present
			(!apply_section_filter || csect == s -> section))
		{
			mame_err = mame_srcdbg_simp_add_global_fixed_symbol(
				mdi_simp_state,
				s -> symbol,
				lw_expr_intval(te),
				(s -> flags & symbol_flag_constant) ? MAME_SRCDBG_SYMFLAG_CONSTANT : 0);
			if (mame_err != MAME_SRCDBG_E_SUCCESS)
			{
				fprintf(stderr, "Error code '%d' trying to add new global variable to MAME debugging information file\n", mame_err);
				return;
			}
		}
		lw_expr_destroy(te);
	}
	
	dump_symbols_aux(as, of, mdi_simp_state, apply_section_filter, csect, se -> right);
}

void do_symdump(asmstate_t *as)
{
	FILE *of;
	
	if (!(as -> flags & FLAG_SYMDUMP))
	{
		return;
	}
	else
	{		
		if (as -> symbol_dump_file)
		{
			if (strcmp(as -> symbol_dump_file, "-") == 0)
			{
				of = stdout;
			}
			else
				of = fopen(as -> symbol_dump_file, "w");
		}
		else
			of = stdout;

		if (!of)
		{
			fprintf(stderr, "Cannot open list file; list not generated\n");
			return;
		}
	}
	dump_symbols_aux(
		as, 
		of, 
		NULL /* mdi_simp_state */, 
		0 /* apply_section_filter */, 
		NULL /* sect */, 
		as -> symtab.head);
}
