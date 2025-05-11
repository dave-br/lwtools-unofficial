#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>

#include <lw_alloc.h>
#include <lw_string.h>
#include <lw_stringlist.h>

#include "lwlink.h"

#include <../../mame/src/lib/srcdbg/srcdbg_api.h>

// // Converts a filespec into a full path.  Returns NULL if there was an error
// char * normalize_path(const char * path_in, char * path_out)
// {
// 	// trim "include:" if it appears
// 	if ((strlen(path_in) > 8) && (path_in[7] == ':')) path_in += 8;
// 	// TODO: Other parts of the code that trim include also trim leading whitespace.  Why?

// 	// relative to absolute path
// 	// TODO: realpath is POSIX-only, but appears to work on Windows builds as well due to
// 	// cygwin.  Are there build targets where realpath doesn't exist?
// 	return realpath(path_in, path_out);
// }

// typedef struct
// {
// 	lw_stringlist_t list;
// 	void * mdi_simp_state;
// } file_path_map;


// file_path_map * fpm_create(void * mdi_simp_state_p)
// {
// 	file_path_map * fpm = lw_alloc(sizeof(file_path_map));
// 	fpm->list = lw_stringlist_create();
// 	fpm->mdi_simp_state = mdi_simp_state_p;
// 	return fpm;
// }

// unsigned short search_list_for_file(lw_stringlist_t list, const char * file)
// {
// 	unsigned short ret = 0;
// 	const char * file_cur = NULL;

// 	lw_stringlist_reset(list);

// 	while ((file_cur = lw_stringlist_current(list)) != NULL)
// 	{
// 		if (strcmp(file_cur, file) == 0)
// 		{
// 			return ret;
// 		}
// 		ret++;
// 		lw_stringlist_next(list);
// 	}

// 	return (unsigned short) -1;
// }

// unsigned short fpm_file_to_index(file_path_map * fpm, char * file)
// {
// 	char normalized_path[PATH_MAX+1];
// 	unsigned short ret;
	
// 	// Is filespec already cached?
// 	ret = search_list_for_file(fpm->list, file);
// 	if (ret != (unsigned short) -1)
// 	{
// 		// Already cached, done
// 		return ret;
// 	}

// 	// Convert filespec to absolute path
// 	if (normalize_path(file, normalized_path) == NULL)
// 	{
// 		return (unsigned short) -1;
// 	}

// 	// Inform dbginfo builder of new path
// 	mame_srcdbg_simp_add_source_file_path(fpm->mdi_simp_state, normalized_path, &ret);

// 	// Cache new path
// 	lw_stringlist_addstring(fpm->list, file);

// 	// We rely on dbginfo builder's guarantee to dole out file indexes
// 	// contiguously from 0 on.
// 	assert(search_list_for_file(fpm->list, file) == ret);
// 	return ret;
// }

// void fpm_destroy(file_path_map * fpm)
// {
// 	lw_stringlist_destroy(fpm->list);
// 	lw_free(fpm);
// }


// TODO: SHARE WITH LWASM
void get_mdi_name(char *mdi_name, const char *mdi_dir, const char *mdi_base_name, const char *section_name)
{
	// TODO: NEEDS SIZE CHECKING, ERROR HANDLING
	if (mdi_dir)
	{
		sprintf(mdi_name, "%s/%s_%s.mdi", mdi_dir, mdi_base_name, section_name);
	}
	else
	{
		sprintf(mdi_name, "%s_%s.mdi", mdi_base_name, section_name);
	}
}


void do_mame_dump()
{
	void * mdi_simp_state = NULL; 
	int sn;
	char imported_mdi_name[PATH_MAX+1];
	char error[1024];

	if (mame_srcdbg_simp_open_new(mdi_file, &mdi_simp_state) != 0)
	{
		fprintf(stderr, "Cannot create MAME debugging information file '%s'\n", mdi_file);
		return;
	}

	// display section list
	for (sn = 0; sn < nsects; sn++)
	{
		const char * sectname = sanitize_symbol((char*)(sectlist[sn].ptr -> name));
		const char * sectfile = sectlist[sn].ptr -> file -> filename;
		const char * sectdir = sectlist[sn].ptr -> file -> filedir;
		while (sectdir == NULL && sectlist[sn].ptr -> file -> parent != NULL)
		{
			sectdir = sectlist[sn].ptr -> file -> parent -> filedir;
		}

		get_mdi_name(imported_mdi_name, sectdir, sectfile, sectname);

		// TODO: ERROR
		if (mame_srcdbg_simp_import(mdi_simp_state, imported_mdi_name, sectlist[sn].ptr -> loadaddress, error, sizeof(error) != MAME_SRCDBG_E_SUCCESS))
		{
			fprintf(stderr, "Warning: unable to import debugging information from %s, section %s.  This information will be missing from the generated debugging information file.\n", sectfile, sectname);
		}
	}

	mame_srcdbg_simp_close(mdi_simp_state);
	mdi_simp_state = NULL;
}