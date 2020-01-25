/*
Copyright (C) 2008 German Garcia

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "qcommon.h"
#include "../angelwrap/qas_public.h"

angelwrap_export_t *GetAngelwrapAPI( angelwrap_import_t* );

static angelwrap_export_t *ae;
static mempool_t *com_scriptmodulepool;

static void Com_ScriptModule_Error( const char *msg )
{
	Com_Error( ERR_DROP, "%s", msg );
}

static void Com_ScriptModule_Print( const char *msg )
{
	Com_Printf( "%s", msg );
}

static void *Com_ScriptModule_MemAlloc( mempool_t *pool, size_t size, const char *filename, int fileline )
{
	return _Mem_Alloc( pool, size, MEMPOOL_ANGELSCRIPT, 0, filename, fileline );
}

static void Com_ScriptModule_MemFree( void *data, const char *filename, int fileline )
{
	_Mem_Free( data, MEMPOOL_ANGELSCRIPT, 0, filename, fileline );
}

static mempool_t *Com_ScriptModule_MemAllocPool( const char *name, const char *filename, int fileline )
{
	return _Mem_AllocPool( com_scriptmodulepool, name, MEMPOOL_ANGELSCRIPT, filename, fileline );
}

static void Com_ScriptModule_MemFreePool( mempool_t **pool, const char *filename, int fileline )
{
	_Mem_FreePool( pool, MEMPOOL_ANGELSCRIPT, 0, filename, fileline );
}

static void Com_ScriptModule_MemEmptyPool( mempool_t *pool, const char *filename, int fileline )
{
	_Mem_EmptyPool( pool, MEMPOOL_ANGELSCRIPT, 0, filename, fileline );
}

/*
* Com_LoadScriptLibrary
*/
static void *Com_LoadScriptLibrary( const char *basename, void *parms )
{
	return GetAngelwrapAPI( parms );
}

void Com_ScriptModule_Shutdown( void )
{
	if( !ae )
		return;

	ae->Shutdown();

	Mem_FreePool( &com_scriptmodulepool );
	ae = NULL;
}

static bool Com_ScriptModule_Load( const char *name, angelwrap_import_t *import )
{
	Com_Printf( "Loading %s module.\n", name );

	ae = (angelwrap_export_t *)Com_LoadScriptLibrary( name, import );
	if( !ae )
	{
		Com_Printf( "Loading %s failed\n", name );
		return false;
	}

	if( !ae->Init() )
	{
		ae = NULL;
		Com_Printf( "Initialization of %s failed\n", name );
		return false;
	}

	Com_Printf( "Initialization of %s successful\n", name );

	return true;
}

void Com_ScriptModule_Init( void )
{
	angelwrap_import_t import;
	static const char *name = "angelwrap";

	Com_ScriptModule_Shutdown();

	//if( !com_angelscript->integer )
	//{
	//	if( verbose )
	//	{
	//		Com_Printf( "Not loading angel script module\n" );
	//		Com_Printf( "------------------------------------\n" );
	//	}
	//	return;
	//}

	Com_Printf( "------- angel script initialization -------\n" );

	com_scriptmodulepool = Mem_AllocPool( NULL, "Angel Script Module" );

	import.Error = Com_ScriptModule_Error;
	import.Print = Com_ScriptModule_Print;

	import.Milliseconds = Sys_Milliseconds;

	import.Cvar_Get = Cvar_Get;
	import.Cvar_Set = Cvar_Set;
	import.Cvar_SetValue = Cvar_SetValue;
	import.Cvar_ForceSet = Cvar_ForceSet;
	import.Cvar_String = Cvar_String;
	import.Cvar_Value = Cvar_Value;

	import.Cmd_Argc = Cmd_Argc;
	import.Cmd_Argv = Cmd_Argv;
	import.Cmd_Args = Cmd_Args;

	import.Cmd_AddCommand = Cmd_AddCommand;
	import.Cmd_RemoveCommand = Cmd_RemoveCommand;
	import.Cmd_ExecuteText = Cbuf_ExecuteText;

	import.Mem_Alloc = Com_ScriptModule_MemAlloc;
	import.Mem_Free = Com_ScriptModule_MemFree;
	import.Mem_AllocPool = Com_ScriptModule_MemAllocPool;
	import.Mem_FreePool = Com_ScriptModule_MemFreePool;
	import.Mem_EmptyPool = Com_ScriptModule_MemEmptyPool;

	// load the actual library
	if( !Com_ScriptModule_Load( name, &import ) )
	{
		Mem_FreePool( &com_scriptmodulepool );
		ae = NULL;
		return;
	}

	// check memory integrity
	Mem_DebugCheckSentinelsGlobal();

	Com_Printf( "------------------------------------\n" );
}

/*
* Com_asGetAngelExport
* Fixme: This should be improved to include some kind of API validation
*/
struct angelwrap_api_s *Com_asGetAngelExport( void )
{
	if( !ae )
		return NULL;

	return ae->asGetAngelExport();
}
