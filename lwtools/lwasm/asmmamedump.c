#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <lw_alloc.h>
#include <lw_string.h>
#include <lw_stringlist.h>

#include "lwasm.h"
#include "instab.h"

#include <srcdbg_api.h>

// realpath is POSIX-only, _fullpath works on Windows.
// TODO: What about mac?
#ifdef WIN32
    #define realpath(N,R) _fullpath((R),(N),PATH_MAX)
#endif

// Converts a filespec into a full path.  Returns NULL if there was an error
char * normalize_path(const char * path_in, char * path_out)
{
	// trim "include:" if it appears
	if ((strlen(path_in) > 8) && (path_in[7] == ':')) path_in += 8;
	// TODO: Other parts of the code that trim include also trim leading whitespace.  Why?

	// relative to absolute path
	return realpath(path_in, path_out);
}

typedef struct
{
	lw_stringlist_t list;
	void * mdi_simp_state;
} file_path_map;


file_path_map * fpm_create(void * mdi_simp_state_p)
{
	file_path_map * fpm = lw_alloc(sizeof(file_path_map));
	fpm->list = lw_stringlist_create();
	fpm->mdi_simp_state = mdi_simp_state_p;
	return fpm;
}

unsigned int search_list_for_file(lw_stringlist_t list, const char * file)
{
	unsigned int ret = 0;
	const char * file_cur = NULL;

	lw_stringlist_reset(list);

	while ((file_cur = lw_stringlist_current(list)) != NULL)
	{
		if (strcmp(file_cur, file) == 0)
		{
			return ret;
		}
		ret++;
		lw_stringlist_next(list);
	}

	return (unsigned int) -1;
}

unsigned short fpm_file_to_index(file_path_map * fpm, const char * file)
{
	// as -> output_format = OUTPUT_OBJ
	char normalized_path[PATH_MAX+1];
	unsigned int ret;
	int mame_err;
	
	// Is filespec already cached?
	ret = search_list_for_file(fpm->list, file);
	if (ret != (unsigned int) -1)
	{
		// Already cached, done
		return ret;
	}

	// Convert filespec to absolute path
	if (normalize_path(file, normalized_path) == NULL)
	{
		fprintf(stderr, "Error while writing MAME debugging information file: unable to normalize file path '%s'\n", file);
		return (unsigned short) -1;
	}

	// Inform dbginfo builder of new path
	mame_err = mame_srcdbg_simp_add_source_file_path(fpm->mdi_simp_state, normalized_path, &ret);
	if (mame_err != MAME_SRCDBG_E_SUCCESS)
	{
		fprintf(stderr, "Error code '%d' trying to add source file path '%s'\n", mame_err, normalized_path);
		return (unsigned short) -1;
	}

	// Cache new path
	lw_stringlist_addstring(fpm->list, file);

	// We rely on dbginfo builder's guarantee to dole out file indexes
	// contiguously from 0 on.
	assert(search_list_for_file(fpm->list, file) == ret);
	return ret;
}

void fpm_destroy(file_path_map * fpm)
{
	lw_stringlist_destroy(fpm->list);
	lw_free(fpm);
}

// Creates a filename from mdi_name, but with section_name inserted just before the extension
void get_mdi_name(char *mdi_name, const char *mdi_base_name, const char *section_name)
{
	// TODO: NEEDS SIZE CHECKING, ERROR HANDLING
	sprintf(
		mdi_name,
		"%s%s%s.mdi",
		mdi_base_name, 
		(section_name == NULL ? "" : "_"), 
		(section_name == NULL ? "" : section_name));
}

void dump_symbols_aux(asmstate_t *as, FILE *of, void *mdi_simp_state, sectiontab_t *csect, struct symtabe *se);

void finalize_section_dump(void **mdi_simp_state, asmstate_t *as, file_path_map **filemap)
{
	int mame_err;
	assert (*mdi_simp_state != NULL);

	dump_symbols_aux(as, NULL /* list file */, *mdi_simp_state, as -> csect, as -> symtab.head);

	fpm_destroy(*filemap);
	*filemap = NULL;

	mame_err = mame_srcdbg_simp_close(*mdi_simp_state);
	if (mame_err != MAME_SRCDBG_E_SUCCESS)
	{
		fprintf(stderr, "Error code '%d' trying to close MAME debuggin information file\n", mame_err);
		return (unsigned short) -1;
	}
	*mdi_simp_state = NULL;
}

void do_mame_dump(asmstate_t *as)
{
	void * mdi_simp_state = NULL; 
	line_t *cl, *nl;
	file_path_map * filemap = NULL;
	char mdi_name[PATH_MAX+1];
	int mame_err;

	// if (!(as -> flags & FLAG_MDI) || !as->mame_dbg_file)
	if (!(as -> flags & FLAG_MDI))
		return;

	as -> csect = NULL;

	for (cl = as -> line_head; cl; cl = nl)
	{
		lw_expr_t te;

		nl = cl -> next;

		if (cl -> len < 1 && cl -> dlen < 1)
			continue;

		if ((cl -> insn >= 0) && (instab[cl -> insn].flags & lwasm_insn_setdata))
			continue;

		if (cl->lineno < 0)
		{
			// TODO: IS THIS POSSIBLE?
			continue;
		}

		te = lw_expr_copy(cl -> addr);
		as -> exportcheck = 1;
		if (mdi_simp_state == NULL || (as -> csect != cl -> csect && cl -> csect != NULL))
		{
			// This line starts a new section

			// Close current mdi, if any
			if (mdi_simp_state != NULL)
			{
				finalize_section_dump(&mdi_simp_state, as, &filemap);
			}

			// Open new mdi for this section
			get_mdi_name(mdi_name, as->output_file, (cl -> csect == NULL ? NULL : cl -> csect -> name));
			mame_err = mame_srcdbg_simp_open_new(mdi_name, &mdi_simp_state);
			if (mame_err != MAME_SRCDBG_E_SUCCESS)
			{
				fprintf(stderr, "Error code '%d', errno '%d', trying to create new MAME debugging information file '%s'\n", mame_err, errno, mdi_name);
				return;
			}
			filemap = fpm_create(mdi_simp_state);
		}
		as -> csect = cl -> csect;
		lwasm_reduce_expr(as, te);
		as -> exportcheck = 0;
		unsigned short addr = (unsigned short) (lw_expr_intval(te) & 0xffff);
		unsigned short file_idx = fpm_file_to_index(filemap, cl->file_path);
		if (file_idx == (unsigned short) -1)
		{
			// Error already reported from fpm_file_to_index
			return;
		}
		mame_err = mame_srcdbg_simp_add_line_mapping(mdi_simp_state, addr, addr, file_idx, (unsigned int) cl->lineno);
		if (mame_err != MAME_SRCDBG_E_SUCCESS)
		{
			fprintf(stderr, "Error code '%d' trying to add new line mapping to MAME debuggin information file\n", mame_err);
			return;
		}
		lw_expr_destroy(te);
	}

	if (mdi_simp_state != NULL)
	{
		finalize_section_dump(&mdi_simp_state, as, &filemap);
	}
}