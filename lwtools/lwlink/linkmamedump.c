#include <errno.h>
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
void get_mdi_name(char *mdi_name, const char *mdi_dir, const char *mdi_base_name, const char *section_name, int relocated)
{
	// TODO: NEEDS SIZE CHECKING, ERROR HANDLING
	*mdi_name = '\0';
	if (mdi_dir)
	{
		strcat(mdi_name, mdi_dir);
		strcat(mdi_name, "/");
	}
	strcat(mdi_name, mdi_base_name);
	strcat(mdi_name, "_");
	strcat(mdi_name, section_name);
	if (relocated)
	{
		strcat(mdi_name, "_relocated");
	}
	strcat(mdi_name, ".mdi");
}


void do_mame_dump()
{
	void * mdi_simp_state = NULL; 
	void * mdi_simp_state_relocated = NULL;
	int sn;
	char imported_mdi_name[PATH_MAX+1];
	char relocated_mdi_name[PATH_MAX+1];
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

		get_mdi_name(imported_mdi_name, sectdir, sectfile, sectname, 0 /* relocated */);
		
		// Import intermediate MDI into linked MDI
		mame_err = mame_srcdbg_simp_import(mdi_simp_state, imported_mdi_name, sectlist[sn].ptr -> loadaddress, mame_import_error_message, sizeof(mame_import_error_message));
		if (mame_err != MAME_SRCDBG_E_SUCCESS)
		{
			// TODO: Could tighten this into a proper error if user could
			// specify which MDIs they want to import.  CMOC only needs
			// code section for user code, but also whatever sections
			// are available from stdlib/float.
			fprintf(stderr, "Warning: code '%d', trying to import MAME debugging information file '%s'\n", mame_err, imported_mdi_name);
			fprintf(stderr, "%s\n", mame_import_error_message);
			fprintf(stderr, "This information will be missing from the generated debugging information file.\n");
			continue;
		}

		// Import intermediate MDI into its own relocated MDI.  Useful in cases
		// where different code gets swapped into and out of the same logical
		// address space (using MMU or SAM)
		// TODO: This doubles the MDI crud on the hard drive, so should add a
		// cmd line param to turn this on explicitly
		get_mdi_name(relocated_mdi_name, sectdir, sectfile, sectname, 1 /* relocated */);
		mame_err = mame_srcdbg_simp_open_new(relocated_mdi_name, &mdi_simp_state_relocated);
		if (mame_err != MAME_SRCDBG_E_SUCCESS)
		{
			fprintf(stderr, "Error code '%d', errno '%d', trying to create relocated MAME debugging information file '%s'\n", mame_err, errno, relocated_mdi_name);
			continue;
		}
		mame_err = mame_srcdbg_simp_import(mdi_simp_state_relocated, imported_mdi_name, sectlist[sn].ptr -> loadaddress, mame_import_error_message, sizeof(mame_import_error_message));
		if (mame_err != MAME_SRCDBG_E_SUCCESS)
		{
			fprintf(stderr, "Warning: code '%d', trying to import MAME debugging information file '%s' into relocated MDI '%s'\n", mame_err, imported_mdi_name, relocated_mdi_name);
			fprintf(stderr, "%s\n", mame_import_error_message);
			mame_srcdbg_simp_close(mdi_simp_state_relocated);
			mdi_simp_state_relocated = NULL;
			continue;
		}
		mame_err = mame_srcdbg_simp_close(mdi_simp_state_relocated);
		if (mame_err != MAME_SRCDBG_E_SUCCESS)
		{
			fprintf(stderr, "Error code '%d' trying to close relocated MAME debugging information file '%s'\n", mame_err, relocated_mdi_name);
		}
		mdi_simp_state_relocated = NULL;
	}

	mame_err = mame_srcdbg_simp_close(mdi_simp_state);
	if (mame_err != MAME_SRCDBG_E_SUCCESS)
	{
		fprintf(stderr, "Error code '%d' trying to close MAME debugging information file\n", mame_err);
	}
	mdi_simp_state = NULL;
}