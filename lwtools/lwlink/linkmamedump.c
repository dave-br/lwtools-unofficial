#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>

#include <lw_alloc.h>
#include <lw_string.h>
#include <lw_stringlist.h>

#include "lwlink.h"

#include <srcdbg_api.h>

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
	char mame_import_error_message[1024];
	int mame_err;

	mame_err = mame_srcdbg_simp_open_new(mdi_file, &mdi_simp_state);
	if (mame_err != MAME_SRCDBG_E_SUCCESS)
	{
		fprintf(stderr, "Error code '%d', errno '%d', trying to create new MAME debugging information file '%s'\n", mame_err, errno, mdi_file);
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

		mame_err = mame_srcdbg_simp_import(mdi_simp_state, imported_mdi_name, sectlist[sn].ptr -> loadaddress, mame_import_error_message, sizeof(mame_import_error_message));
		if (mame_err != MAME_SRCDBG_E_SUCCESS)
		{
			fprintf(stderr, "Error code '%d', trying to import MAME debugging information file '%s'\n", mame_err, imported_mdi_name);
			fprintf(stderr, "%s\n", mame_import_error_message);
			return;
		}
	}

	mame_err = mame_srcdbg_simp_close(mdi_simp_state);
	if (mame_err != MAME_SRCDBG_E_SUCCESS)
	{
		fprintf(stderr, "Error code '%d' trying to close MAME debuggin information file\n", mame_err);
	}
	mdi_simp_state = NULL;
}