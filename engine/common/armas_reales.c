/*
armas_reales.c - what the weapons weigh and how precise they really are
Copyright (C) 2026 Oscar Soler

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"

/*
The realistic precision (precision_realista) needs what the game doesn't
have: the real weight of each weapon, for the sway of the aim (cl_vaiven.c),
and how far off its bullets go by themselves, in minutes of angle, for where
they hit aiming down the sights (ads_bala.c). They are in precision.txt of
the game folder, a line each:

  <view model> <kg> <moa>

The view model is the start of the name (models/v_ak74 is v_ak74.mdl and
v_ak74_2.mdl too, the longest that fits counts) and a moa of - leaves the
bullets as the game spreads them (shotguns: their pellets are spread for
real). Lines starting with // are comments.
*/

#define MAX_ARMAS_REALES	64
#define ARMA_PESO		4.0f	// what isn't in the file
#define ARMA_MOA		3.0f

typedef struct
{
	char	modelo[64];
	float	kg;
	float	moa;	// < 0: the game's own spread
} arma_real_t;

static arma_real_t	armas[MAX_ARMAS_REALES];
static int		num_armas;
static qboolean		cargadas;

static void COM_ArmasRealesCargar( void )
{
	char	*afile, *linea, *siguiente;

	cargadas = true;
	num_armas = 0;

	afile = (char *)FS_LoadFile( "precision.txt", NULL, false );
	if( !afile )
		return;

	for( linea = afile; linea && *linea && num_armas < MAX_ARMAS_REALES; linea = siguiente )
	{
		arma_real_t	*a = &armas[num_armas];
		char		moa[16];

		siguiente = Q_strchr( linea, '\n' );
		if( siguiente )
			*siguiente++ = '\0';

		if( sscanf( linea, "%63s %f %15s", a->modelo, &a->kg, moa ) != 3 || a->modelo[0] == '/' )
			continue;

		a->moa = moa[0] == '-' ? -1.0f : Q_atof( moa );
		num_armas++;
	}

	Mem_Free( afile );
}

/*
==============
COM_ArmaReal

the weight in kg of the weapon with this view model, and in moa how far off
its bullets go by themselves (< 0: as the game spreads them)
==============
*/
float COM_ArmaReal( const char *modelo, float *moa )
{
	const arma_real_t	*mejor = NULL;
	size_t			largo = 0;
	int			i;

	if( !cargadas )
		COM_ArmasRealesCargar();

	for( i = 0; modelo && i < num_armas; i++ )
	{
		size_t l = Q_strlen( armas[i].modelo );

		if( l > largo && !Q_strnicmp( modelo, armas[i].modelo, l ))
		{
			mejor = &armas[i];
			largo = l;
		}
	}

	if( moa )
		*moa = mejor ? mejor->moa : ARMA_MOA;
	return mejor ? mejor->kg : ARMA_PESO;
}

/*
==============
COM_ArmasRealesRecargar

precision.txt again, after it was changed
==============
*/
void COM_ArmasRealesRecargar( void )
{
	cargadas = false;
}
