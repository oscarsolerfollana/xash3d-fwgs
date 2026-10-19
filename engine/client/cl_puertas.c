/*
cl_puertas.c - light coming through open doors
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
#include "client.h"
#include "cl_tent.h"

/*
The light of the maps is baked: a door that opens does not let the light of the
other side in. For each brush entity that moved from where it started (a door,
sliding or turning), the light on both sides of it measured while it was closed:
when it opens, a dynamic light on the dark side with the colour of the bright
one, as big as the opening.
*/

CVAR_DEFINE_AUTO( cl_luz_puertas, "1", FCVAR_ARCHIVE, "custom: the light of the other side comes in when a door opens (0 off, 2 twice as strong)" );

#define MAX_PUERTAS	1024

static struct
{
	qboolean	medida;		// light measured while closed
	vec3_t		claro;		// colour of the bright side
	vec3_t		hacia;		// from the door to the dark side
	float		fuerza;		// bright minus dark
	int		servercount;
} puertas[MAX_PUERTAS];

static float CL_PuertaBrillo( const colorVec *c )
{
	return ( c->r + c->g + c->b ) / 3.0f;
}

void CL_InitPuertas( void )
{
	Cvar_RegisterVariable( &cl_luz_puertas );
}

void CL_LuzPuertas( void )
{
	if( !cl_luz_puertas.value || !cl.worldmodel )
		return;

	for( int i = 1; i < clgame.maxEntities && i < MAX_PUERTAS; i++ )
	{
		cl_entity_t *e = CL_GetEntityByIndex( i );
		model_t *m;
		vec3_t size, centro, n, abierto;
		float movido, grosor, hueco, frac;
		int eje = 0;

		if( !e || !( m = e->model ) || m->type != mod_brush || m->name[0] != '*' )
			continue;
		if( e->curstate.messagenum != cl.parsecount )
			continue;	// not in this update
		if( e->curstate.rendermode != kRenderNormal )
			continue;

		// thin axis of the brush: the way through it
		VectorSubtract( m->maxs, m->mins, size );
		if( size[1] < size[eje] ) eje = 1;
		if( size[2] < size[eje] ) eje = 2;
		if( eje == 2 )
			continue;	// a platform, a lift: not a door
		grosor = size[eje];
		if( grosor > 48.0f || size[2] < 48.0f )
			continue;

		VectorAdd( m->mins, m->maxs, centro );
		VectorScale( centro, 0.5f, centro );
		VectorAdd( centro, e->baseline.origin, centro );
		VectorClear( n );
		n[eje] = 1.0f;

		VectorSubtract( e->curstate.origin, e->baseline.origin, abierto );
		movido = VectorLength( abierto );
		{
			float giro = fabs( e->curstate.angles[YAW] - e->baseline.angles[YAW] );
			if( giro > 180.0f ) giro = 360.0f - giro;
			movido = Q_max( movido, giro * 1.2f );
		}

		if( puertas[i].servercount != cl.servercount )
		{
			puertas[i].medida = false;
			puertas[i].servercount = cl.servercount;
		}

		if( movido < 1.0f && !( cl_luz_puertas.value >= 9.0f && puertas[i].medida ))	// 9: as if open, for tests
		{
			// closed: what is on each side, without any light of ours in it yet
			vec3_t a, b;
			colorVec la, lb;
			float fa, fb;

			VectorMA( centro, grosor * 0.5f + 24.0f, n, a );
			VectorMA( centro, -( grosor * 0.5f + 24.0f ), n, b );
			la = ref.dllFuncs.R_LightPoint( a );
			lb = ref.dllFuncs.R_LightPoint( b );
			fa = CL_PuertaBrillo( &la );
			fb = CL_PuertaBrillo( &lb );
			puertas[i].medida = true;
			if( fa >= fb )
			{
				VectorSet( puertas[i].claro, la.r, la.g, la.b );
				VectorNegate( n, puertas[i].hacia );
				puertas[i].fuerza = fa - fb;
			}
			else
			{
				VectorSet( puertas[i].claro, lb.r, lb.g, lb.b );
				VectorCopy( n, puertas[i].hacia );
				puertas[i].fuerza = fb - fa;
			}
			continue;
		}

		if( !puertas[i].medida || puertas[i].fuerza < 12.0f )
			continue;

		// how open: what it moved over its own width (sliding) or the turn (rotating)
		hueco = Q_max( size[eje == 0 ? 1 : 0], 32.0f );
		frac = cl_luz_puertas.value >= 9.0f ? 1.0f : bound( 0.0f, movido / hueco, 1.0f );
		if( cl_luz_puertas.value >= 9.0f )
			Con_NPrintf( 20 + ( i % 10 ), "puerta %d: claro %.0f %.0f %.0f, fuerza %.0f, radio %.0f", i, puertas[i].claro[0], puertas[i].claro[1], puertas[i].claro[2], puertas[i].fuerza, ( hueco + size[2] ) * 0.9f );

		{
			dlight_t *dl = CL_AllocDlight( 0x7000 + i );
			// the size of the opening sets how far it reaches; how much brighter the other side is, how bright
			float k = bound( 0.0f, cl_luz_puertas.value >= 9.0f ? 1.0f : cl_luz_puertas.value, 2.0f ) * Q_min( 0.4f + puertas[i].fuerza / 40.0f, 1.0f );
			float maxc = Q_max( puertas[i].claro[0], Q_max( puertas[i].claro[1], puertas[i].claro[2] ));

			VectorMA( centro, grosor * 0.5f + 40.0f, puertas[i].hacia, dl->origin );
			dl->radius = ( hueco + size[2] ) * 1.3f * frac;
			if( maxc < 1.0f ) maxc = 1.0f;
			dl->color.r = bound( 0, (int)( puertas[i].claro[0] / maxc * 255.0f * k ), 255 );
			dl->color.g = bound( 0, (int)( puertas[i].claro[1] / maxc * 255.0f * k ), 255 );
			dl->color.b = bound( 0, (int)( puertas[i].claro[2] / maxc * 255.0f * k ), 255 );
			dl->die = cl.time + 0.15f;
			dl->decay = 0.0f;
		}
	}
}
