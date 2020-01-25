/*
Copyright (C) 2014 Victor Luchits

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
#include "../qcommon/qcommon.h"

static bool steamlib_initialized = false;

typedef void ( *com_error_t )( int code, const char *format, ... );

/*
* Steam_Init
*/
void Steam_Init( void )
{
	steamlib_initialized = true;
}

/*
* Steam_RunFrame
*/
void Steam_RunFrame( void )
{
	if( steamlib_initialized ) {
		/* WTF */
	}
}

/*
* Steam_Shutdown
*/
void Steam_Shutdown( void )
{
	if( steamlib_initialized ) {
		/* WTF */
	}
}

/*
* Steam_GetSteamID
*/
uint64_t Steam_GetSteamID( void )
{
	if( steamlib_initialized ) {
		return 0; /* WTF */
	}
	return 0;
}

/*
* Steam_GetAuthSessionTicket
*/
int Steam_GetAuthSessionTicket( void (*callback)( void *, size_t ) )
{
	if( steamlib_initialized ) {
		return 0; /* WTF */
	}
	return 0;
}

/*
* Steam_AdvertiseGame
*/
void Steam_AdvertiseGame( const uint8_t *ip, unsigned short port )
{
	if( steamlib_initialized ) {
		/* WTF */
	}
}

/*
* Steam_GetPersonaName
*/
void Steam_GetPersonaName( char *name, size_t namesize )
{
	if( !namesize ) {
		return;
	}

	if( steamlib_initialized ) {
		name[0] = '\0'; /* WTF */
	} else {
		name[0] = '\0';
	}
}
