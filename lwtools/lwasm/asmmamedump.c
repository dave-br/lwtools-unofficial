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

typedef struct
{
	lw_stringlist_t list;
	void * mdi_simp_state;
} file_path_map;

typedef struct mditab_s mditab_t;
struct mditab_s
{
	sectiontab_t * section;
	void * mdi_simp_state;
	file_path_map * filemap;

	mditab_t * next;
};

// Converts a filespec into a full path.  Returns NULL if there was an error
char * normalize_path(const char * path_in, char * path_out)
{
	// trim "include:" if it appears
	if ((strlen(path_in) > 8) && (path_in[7] == ':')) path_in += 8;
	// TODO: Other parts of the code that trim include also trim leading whitespace.  Why?

	// relative to absolute path
	return realpath(path_in, path_out);
}

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
		fprintf(stderr, "Error code '%d' trying to close MAME debugging information file\n", mame_err);
	}
	*mdi_simp_state = NULL;
}

int mdi_for_section(asmstate_t *as, mditab_t ** mdis, sectiontab_t * section, mditab_t ** requested_mdi)
{
	mditab_t * mdi = NULL;
	char mdi_name[PATH_MAX+1];
	int mame_err;
	void * mdi_simp_state = NULL;

	for (mdi = *mdis; mdi; mdi = mdi -> next)
	{
		if (mdi->section == section)
			break;
	}

	if (mdi != NULL)
	{
		*requested_mdi = mdi;
		return 0;
	}

	// Open new mdi for this section
	get_mdi_name(mdi_name, as->output_file, (section == NULL ? NULL : section -> name));
	mame_err = mame_srcdbg_simp_open_new(mdi_name, &mdi_simp_state);
	if (mame_err != MAME_SRCDBG_E_SUCCESS)
	{
		fprintf(stderr, "Error code '%d', errno '%d', trying to create new MAME debugging information file '%s'\n", mame_err, errno, mdi_name);
		return 1;
	}

	// Allocate new mdi table entry
	// TODO: MEMORY LEAK
	mdi = lw_alloc(sizeof(mditab_t));
	mdi -> section = section;
	mdi -> filemap = fpm_create(mdi_simp_state);
	mdi -> mdi_simp_state = mdi_simp_state;
	mdi -> next = *mdis;
	*mdis = mdi;

	*requested_mdi = mdi;
	return 0;
}


void do_mame_dump(asmstate_t *as)
{
	// void * mdi_simp_state = NULL; 
	mditab_t * mdis = NULL;
	mditab_t * mdi_cur = NULL;
	line_t *cl, *nl;
	// file_path_map * filemap = NULL;
	int mame_err;

	// if (!(as -> flags & FLAG_MDI) || !as->mame_dbg_file)
	if (!(as -> flags & FLAG_MDI))
		return;

	as -> csect = NULL;

	// Boostrap MDI table with a single MDI for code outside of all sections
	if (mdi_for_section(as, &mdis, NULL /* section */, &mdi_cur) != 0)
		return;

	for (cl = as -> line_head; cl; cl = nl)
	{
		lw_expr_t te;

		if (cl->csect != NULL && cl->csect->name != NULL
			&& strcmp(cl->csect->name, "_constants") == 0)
		{
			printf("gotcha\n");
		}

		nl = cl -> next;

		// if (/* mdi_simp_state == NULL || */ (as -> csect != cl -> csect /* && cl -> csect != NULL */))
		if (mdi_cur -> section != cl -> csect)
		{
			// This line starts a new section
			if (mdi_for_section(as, &mdis, cl -> csect, &mdi_cur) != 0)
				return;
		}

		as -> csect = cl -> csect;

		if (cl -> len < 1 && cl -> dlen < 1)
			continue;

		if ((cl -> insn >= 0) && (instab[cl -> insn].flags & lwasm_insn_setdata))
			continue;

		if (cl->lineno < 0)
		{
			// TODO: IS THIS POSSIBLE?
			continue;
		}

		as -> exportcheck = 1;
		te = lw_expr_copy(cl -> addr);
		lwasm_reduce_expr(as, te);
		as -> exportcheck = 0;
		unsigned short addr = (unsigned short) (lw_expr_intval(te) & 0xffff);
		unsigned short file_idx = fpm_file_to_index(mdi_cur -> filemap, cl->file_path);
		if (file_idx == (unsigned short) -1)
		{
			// Error already reported from fpm_file_to_index
			return;
		}
		mame_err = mame_srcdbg_simp_add_line_mapping(
			mdi_cur -> mdi_simp_state, addr, addr, file_idx, (unsigned int) cl -> lineno);
		if (mame_err != MAME_SRCDBG_E_SUCCESS)
		{
			fprintf(stderr, "Error code '%d' trying to add new line mapping to MAME debugging information file\n", mame_err);
			return;
		}
		lw_expr_destroy(te);
	}

	// For each mdi opened, add symbols to it, then write it to disk
	for (mdi_cur = mdis; mdi_cur; mdi_cur = mdi_cur -> next)
	{
		finalize_section_dump(&mdi_cur -> mdi_simp_state, as, &mdi_cur -> filemap);
	}
	// TODO: DELETE mdis
}