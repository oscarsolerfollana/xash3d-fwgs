/*
cl_humo.c - Firearms: volumetric smoke for the artillery marker
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
#include "entity_types.h"

/*
Firearms' client.dll puts the smoke of the artillery marker (events/marker.sc)
as a few flat sprites. Here the event is taken before it and the smoke is made of
many puffs instead (sprites/humo_vol.spr, patches/marcador_cs.py): they come out
of the can, go up and spread with a bit of wind, growing and fading, each one lit
by the light of the map where it is and tinted red or blue, as the game's, by the team. The
renderer draws them soft where they cross the walls and the floor (gl_humo.c).
The sounds of the game's smoke are played as it did: the start, the loop every
4.5 seconds and the end in the last 4.
Without the sprite, or with cl_humo 0, the client.dll does it as always.
*/

static CVAR_DEFINE_AUTO( cl_humo, "1", FCVAR_ARCHIVE, "custom: Firearms: the smoke of the artillery marker made of many puffs (0 the game's)" );
static CVAR_DEFINE_AUTO( cl_humo_densidad, "1", FCVAR_ARCHIVE, "custom: puffs of the marker smoke per second, times this" );

#define HUMO_SPRITE		"sprites/humo_vol.spr"
#define HUMO_DURACION	60.0f	// as the game's
#define HUMO_FIN		4.0f	// the end sound, these seconds before
#define HUMO_BUCLE		4.5f	// the loop sound, every
#define MAX_EMISORES	16
#define MAX_BOCANADAS	384

typedef struct
{
	qboolean	activo;
	int	clave;		// the event's entity: the thrower
	vec3_t	origen;
	vec3_t	viento;
	vec3_t	color;		// of the team
	double	inicio, fin;
	double	siguiente;	// next puff
	double	sonido;		// next loop sound
	qboolean	final;		// the end sound played
} emisor_t;

typedef struct
{
	qboolean	usada;
	cl_entity_t	ent;
	vec3_t	vel;
	vec3_t	color;
	double	nace;
	float	vida;
	float	tam0, tam1;
	float	giro;
	float	opacidad;
	int	emisor;
	vec3_t	luz;
	int	luz_cuenta;
} bocanada_t;

static emisor_t	emisores[MAX_EMISORES];
static bocanada_t	bocanadas[MAX_BOCANADAS];
static model_t	*humo_sprite;

static float CL_HumoAzar( float a, float b )
{
	return COM_RandomFloat( a, b );
}

static void CL_HumoSonido( const vec3_t origen, const char *nombre )
{
	sound_t s = S_RegisterSound( nombre );

	if( s >= 0 )
		S_StartSound( origen, 0, CHAN_AUTO, s, VOL_NORM, ATTN_NORM, PITCH_NORM, 0 );
}

static void CL_HumoBocanada( emisor_t *em, qboolean estallido )
{
	bocanada_t	*b = NULL;
	float		ang, rapidez;
	int		i;

	for( i = 0; i < MAX_BOCANADAS; i++ )
	{
		if( !bocanadas[i].usada )
		{
			b = &bocanadas[i];
			break;
		}
	}

	if( !b )
		return;

	memset( b, 0, sizeof( *b ));
	b->usada = true;
	b->emisor = em - emisores;
	b->nace = cl.time;
	b->vida = CL_HumoAzar( 10.0f, 15.0f );
	b->tam0 = CL_HumoAzar( 0.1f, 0.15f );
	b->tam1 = CL_HumoAzar( 0.55f, 0.85f );
	b->giro = CL_HumoAzar( -12.0f, 12.0f );
	b->opacidad = CL_HumoAzar( 0.55f, 0.8f );
	VectorCopy( em->color, b->color );

	ang = CL_HumoAzar( 0.0f, M_PI2 );
	rapidez = estallido ? CL_HumoAzar( 50.0f, 90.0f ) : CL_HumoAzar( 15.0f, 40.0f );
	b->vel[0] = cos( ang ) * rapidez;
	b->vel[1] = sin( ang ) * rapidez;
	b->vel[2] = estallido ? CL_HumoAzar( 10.0f, 30.0f ) : CL_HumoAzar( 25.0f, 45.0f );

	b->ent.model = humo_sprite;
	b->ent.curstate.rendermode = kRenderTransAlpha;
	b->ent.curstate.renderamt = 0;
	b->ent.curstate.scale = b->tam0;
	b->ent.curstate.frame = COM_RandomLong( 0, humo_sprite->numframes - 1 );
	b->ent.angles[ROLL] = CL_HumoAzar( 0.0f, 360.0f );
	VectorCopy( em->origen, b->ent.origin );
	b->ent.origin[0] += CL_HumoAzar( -6.0f, 6.0f );
	b->ent.origin[1] += CL_HumoAzar( -6.0f, 6.0f );
	b->ent.origin[2] += CL_HumoAzar( 2.0f, 8.0f );
	VectorCopy( b->ent.origin, b->ent.curstate.origin );
	b->luz_cuenta = 0;
}

/*
==============
CL_HumoEvento

from CL_FireEvent: true when the event is done here
==============
*/
qboolean CL_HumoEvento( const char *nombre, event_args_t *args )
{
	emisor_t	*em = NULL;
	int		i;

	if( !cl_humo.value || Q_strnicmp( GI->gamefolder, "firearms", 8 ) || Q_stricmp( nombre, "events/marker.sc" ))
		return false;

	if( !humo_sprite )
		humo_sprite = CL_LoadClientSprite( HUMO_SPRITE );

	if( !humo_sprite || humo_sprite->type != mod_sprite )
		return false;

	// bparam1: the smoke of that player ends (another marker of his)
	for( i = 0; i < MAX_EMISORES; i++ )
	{
		if( emisores[i].activo && emisores[i].clave == args->entindex )
		{
			em = &emisores[i];
			break;
		}
	}

	if( args->bparam1 )
	{
		if( em && em->fin > cl.time )
			em->fin = cl.time;
		return true;
	}

	if( !em )
	{
		for( i = 0; i < MAX_EMISORES; i++ )
		{
			if( !emisores[i].activo )
			{
				em = &emisores[i];
				break;
			}
		}
	}

	if( !em )
		return true;

	memset( em, 0, sizeof( *em ));
	em->activo = true;
	em->clave = args->entindex;
	VectorCopy( args->origin, em->origen );
	em->origen[2] -= 10.0f; // as the game's

	// iparam1 the team: 1 red, 2 blue, the colours of the game's smoke
	if( args->iparam1 == 1 )
		VectorSet( em->color, 190 / 255.0f, 51 / 255.0f, 38 / 255.0f );
	else if( args->iparam1 == 2 )
		VectorSet( em->color, 66 / 255.0f, 115 / 255.0f, 190 / 255.0f );
	else VectorSet( em->color, 0.92f, 0.92f, 0.92f );

	// a light wind, the same for all its puffs
	{
		float ang = CL_HumoAzar( 0.0f, M_PI2 ), v = CL_HumoAzar( 6.0f, 14.0f );
		VectorSet( em->viento, cos( ang ) * v, sin( ang ) * v, 0.0f );
	}

	em->inicio = cl.time;
	em->fin = cl.time + HUMO_DURACION;
	em->siguiente = cl.time;
	em->sonido = cl.time + HUMO_BUCLE;

	for( i = 0; i < 10; i++ )
		CL_HumoBocanada( em, true );

	CL_HumoSonido( em->origen, "items/smoke_start.wav" );

	return true;
}

static float CL_HumoSuave( float a, float b, float x )
{
	float t = bound( 0.0f, ( x - a ) / ( b - a ), 1.0f );

	return t * t * ( 3.0f - 2.0f * t );
}

/*
==============
CL_HumoFrame

every frame, with the temp entities: new puffs, and the puffs move and are added to the view
==============
*/
void CL_HumoFrame( void )
{
	float	dt = bound( 0.0f, cl.time - cl.oldtime, 0.1f );
	float	frenado = 1.0f - exp( -0.6f * dt );
	int	i;

	if( !humo_sprite || cls.state != ca_active )
		return;

	for( i = 0; i < MAX_EMISORES; i++ )
	{
		emisor_t *em = &emisores[i];
		float ritmo;

		if( !em->activo )
			continue;

		if( cl.time >= em->fin )
		{
			if( !em->final )
				CL_HumoSonido( em->origen, "items/smoke_end.wav" );
			em->activo = false;
			continue;
		}

		if( !em->final && cl.time >= em->fin - HUMO_FIN )
		{
			em->final = true;
			CL_HumoSonido( em->origen, "items/smoke_end.wav" );
		}
		else if( !em->final && cl.time >= em->sonido )
		{
			em->sonido = cl.time + HUMO_BUCLE;
			CL_HumoSonido( em->origen, "items/smoke_loop.wav" );
		}

		// strong at first, less towards the end
		ritmo = 9.0f * Q_max( cl_humo_densidad.value, 0.1f ) * ( 1.0f - 0.6f * CL_HumoSuave( em->fin - 15.0f, em->fin, cl.time ));
		while( em->siguiente <= cl.time )
		{
			CL_HumoBocanada( em, false );
			em->siguiente += 1.0f / ritmo;
		}
	}

	for( i = 0; i < MAX_BOCANADAS; i++ )
	{
		bocanada_t	*b = &bocanadas[i];
		float		t, a, crece;
		vec3_t		hacia, nuevo;
		pmtrace_t	tr;

		if( !b->usada )
			continue;

		t = cl.time - b->nace;
		if( t >= b->vida || t < 0.0f || b->ent.model != humo_sprite )
		{
			b->usada = false;
			continue;
		}

		// it slows down to the wind, still rising a bit
		VectorCopy( emisores[b->emisor].viento, hacia );
		hacia[2] = 12.0f;
		VectorLerp( b->vel, frenado, hacia, b->vel );

		VectorMA( b->ent.origin, dt, b->vel, nuevo );
		tr = CL_TraceLine( b->ent.origin, nuevo, PM_WORLD_ONLY );
		if( tr.fraction < 1.0f )
		{
			// along the wall or the ceiling
			float d = DotProduct( b->vel, tr.plane.normal );
			VectorMA( b->vel, -d, tr.plane.normal, b->vel );
			VectorCopy( tr.endpos, nuevo );
		}
		VectorCopy( nuevo, b->ent.origin );
		VectorCopy( nuevo, b->ent.curstate.origin );

		crece = 1.0f - exp( -t / 3.5f );
		b->ent.curstate.scale = b->tam0 + ( b->tam1 - b->tam0 ) * crece;
		b->ent.angles[ROLL] += b->giro * dt;

		a = b->opacidad * CL_HumoSuave( 0.0f, 0.8f, t ) * ( 1.0f - CL_HumoSuave( b->vida * 0.45f, b->vida, t ));
		b->ent.curstate.renderamt = bound( 0, (int)( a * 255.0f ), 255 );

		// the light of the map where it is, now and then
		if( b->luz_cuenta-- <= 0 )
		{
			colorVec l = ref.dllFuncs.R_LightPoint( b->ent.origin );

			b->luz[0] = bound( 0.45f, l.r / 255.0f * 1.25f + 0.3f, 1.0f );
			b->luz[1] = bound( 0.45f, l.g / 255.0f * 1.25f + 0.3f, 1.0f );
			b->luz[2] = bound( 0.45f, l.b / 255.0f * 1.25f + 0.3f, 1.0f );
			b->luz_cuenta = 6;
		}

		b->ent.curstate.rendercolor.r = bound( 1, (int)( b->color[0] * b->luz[0] * 255.0f ), 255 );
		b->ent.curstate.rendercolor.g = bound( 1, (int)( b->color[1] * b->luz[1] * 255.0f ), 255 );
		b->ent.curstate.rendercolor.b = bound( 1, (int)( b->color[2] * b->luz[2] * 255.0f ), 255 );

		if( b->ent.curstate.renderamt > 0 )
			CL_AddVisibleEntity( &b->ent, ET_TEMPENTITY );
	}
}

/*
==============
CL_HumoLimpiar

a new map: no smoke of the last one, and the sprite loaded again
==============
*/
void CL_HumoLimpiar( void )
{
	memset( emisores, 0, sizeof( emisores ));
	memset( bocanadas, 0, sizeof( bocanadas ));
	humo_sprite = NULL;
}

void CL_InitHumo( void )
{
	Cvar_RegisterVariable( &cl_humo );
	Cvar_RegisterVariable( &cl_humo_densidad );
}
