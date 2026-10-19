/*
sv_marcador.c - Firearms: the artillery smoke marker is thrown
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
#include "server.h"

/*
Firearms puts the artillery marker (useskill marker) in front of the player:
a "marker" entity, MOVETYPE_TOSS, 32 units ahead, that falls to the floor, and
the event events/marker.sc with that point, where the clients put the smoke.
The mortars aim at the entity, wherever it is.

Here it is thrown instead, like a grenade: when the game sends that event the
entity is taken to the eyes of the player and sent off forward (the speed of
the HL hand grenade), and the event is held until it lies on the floor, to
send it from there. The checks of the game where the player stands (outside,
room ahead) are let through, it can be thrown from anywhere; where it ends up
decides whether the mortars see it.
*/

static CVAR_DEFINE_AUTO( sv_marcador_lanzar, "1", 0, "custom: Firearms: the artillery smoke marker is thrown like a grenade" );
static CVAR_DEFINE_AUTO( sv_marcador_fuerza, "650", 0, "custom: Firearms: speed the artillery smoke marker is thrown at, at most (650 as the game's grenades)" );
static CVAR_DEFINE_AUTO( sv_marcador_cuesta, "1", 0, "custom: Firearms: how much the gravity pulls the thrown marker down the slopes (2 twice: it slides on gentler ones and further; 0 it doesn't)" );

typedef struct
{
	int	entidad;	// 0: none
	double	hora;
	int	cuenta;		// its pev->impulse, held back while it flies
	float	vz;		// its vertical speed last frame: going up now, it bounced
	qboolean	suelo;		// has touched the ground
	vec3_t	toque;		// where it first did
	qboolean	rodando;	// on the ground, moved here (SV_MarcadorRodar)
	float	giro;		// about its own axis, lying, in degrees
	vec3_t	paso;		// where it was when it last got anywhere
	double	hora_paso;
	int	flags;
	word	eventindex;
	vec3_t	angles;
	float	fparam1, fparam2;
	int	iparam1, iparam2, bparam1, bparam2;
} marcador_t;

static marcador_t	marcadores[MAX_CLIENTS];	// by thrower

// the box it flies with, and the one it rolls with: the can lying, its own size
static const vec3_t	marcador_mins = { -4.0f, -4.0f, 0.0f }, marcador_maxs = { 4.0f, 4.0f, 8.0f };
static const vec3_t	rodar_mins = { -1.5f, -1.5f, 0.0f }, rodar_maxs = { 1.5f, 1.5f, 2.5f };

#define MARCADOR_RADIO		1.2f	// of the can, for how fast it turns rolling
#define MARCADOR_FRENO_LLANO	650.0f	// u/s^2 rolling on flat ground: it stops soon
#define MARCADOR_FRENO_CUESTA	120.0f	// on a slope (over ~10 degrees) it goes on rolling down
static qboolean	soltando;
static qboolean	comando;	// in the game's "useskill marker"

// Counter-Strike's smoke grenade in the hand, if it was copied to the game (patches/marcador_cs.py)
#define MARCADOR_V_CS	"models/v_humo_cs.mdl"

void SV_MarcadorInit( void )
{
	Cvar_RegisterVariable( &sv_marcador_lanzar );
	Cvar_RegisterVariable( &sv_marcador_fuerza );
	Cvar_RegisterVariable( &sv_marcador_cuesta );
}

void SV_MarcadorActivar( void )
{
	memset( marcadores, 0, sizeof( marcadores ));

	// still loading: the grenade in the hand (cl_marcador.c)
	if( !Q_strnicmp( GI->gamefolder, "firearms", 8 ) && FS_FileExists( MARCADOR_V_CS, false ))
		SV_ModelIndex( MARCADOR_V_CS );
}

/*
==============
SV_MarcadorComando

around the game's client commands
==============
*/
void SV_MarcadorComando( qboolean dentro )
{
	comando = dentro && sv_marcador_lanzar.value && !Q_strnicmp( GI->gamefolder, "firearms", 8 )
		&& Cmd_Argc() == 2 && !Q_stricmp( Cmd_Argv( 0 ), "useskill" ) && !Q_stricmp( Cmd_Argv( 1 ), "marker" );
}

/*
==============
SV_MarcadorFuera

from pfnTraceTexture: thrown, it can go anywhere, so the game's "An artillery
marker must be placed outside" (a line 6000 units up from the player that has
to end on a sky texture) always passes. Where it ends up, SV_MarcadorCielo
decides whether the mortars see it
==============
*/
qboolean SV_MarcadorFuera( void )
{
	return comando;
}

/*
==============
SV_MarcadorSitio

from pfnTraceHull: before it puts the marker down the game wants room for the
large hull 32 units ahead of the player, which slopes and holes don't have
("Cannot place Artillery Marker here"); thrown, it doesn't go there
==============
*/
qboolean SV_MarcadorSitio( const float *v1, const float *v2, int hull, TraceResult *ptr )
{
	if( !comando || hull != 2 || !VectorCompare( v1, v2 ))
		return false;

	memset( ptr, 0, sizeof( *ptr ));
	ptr->flFraction = 1.0f;
	ptr->fInOpen = true;
	VectorCopy( v2, ptr->vecEndPos );
	ptr->pHit = SV_EdictNum( 0 );

	return true;
}

static edict_t *SV_MarcadorBuscar( const float *origin )
{
	int	i;

	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );

		if( !SV_IsValidEdict( e ) || Q_strcmp( SV_GetString( e->v.classname ), "marker" ))
			continue;

		if( VectorDistance2( e->v.origin, origin ) < 1.0f )
			return e;
	}

	return NULL;
}

/*
==============
SV_MarcadorEvento

from SV_PlaybackEventFull: true when the event is held back
==============
*/
qboolean SV_MarcadorEvento( int flags, const edict_t *pInvoker, word eventindex, float *origin, float *angles,
	float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 )
{
	edict_t		*jugador = (edict_t *)pInvoker, *e;
	marcador_t	*m;
	vec3_t		ang, fwd, right, up, ojo, inicio;
	float		vel;
	trace_t		tr;
	int		slot;

	if( soltando || !sv_marcador_lanzar.value || !origin || Q_strnicmp( GI->gamefolder, "firearms", 8 ))
		return false;

	// bparam1: the smoke ends
	if( Q_stricmp( sv.event_precache[eventindex], "events/marker.sc" ) || bparam1 )
		return false;

	if( !SV_IsValidEdict( jugador ))
		return false;

	slot = NUM_FOR_EDICT( jugador ) - 1;
	if( slot < 0 || slot >= svs.maxclients )
		return false;

	e = SV_MarcadorBuscar( origin );
	if( !e )
		return false;

	// as Firearms' grenades: a bit upwards, faster the higher, up to 650
	VectorAdd( jugador->v.v_angle, jugador->v.punchangle, ang );
	if( ang[PITCH] < 0.0f )
		ang[PITCH] = -10.0f + ang[PITCH] * (( 90.0f - 10.0f ) / 90.0f );
	else ang[PITCH] = -10.0f + ang[PITCH] * (( 90.0f + 10.0f ) / 90.0f );
	vel = Q_min(( 90.0f - ang[PITCH] ) * 10.0f, sv_marcador_fuerza.value );
	AngleVectors( ang, fwd, right, up );

	VectorAdd( jugador->v.origin, jugador->v.view_ofs, ojo );
	VectorMA( ojo, 16.0f, fwd, inicio );
	tr = SV_Move( ojo, (float *)marcador_mins, (float *)marcador_maxs, inicio, MOVE_NOMONSTERS, jugador, false );

	SV_SetMinMaxSize( e, marcador_mins, marcador_maxs, false );
	VectorCopy( tr.endpos, e->v.origin );
	VectorScale( fwd, vel, e->v.velocity );
	VectorAdd( e->v.velocity, jugador->v.velocity, e->v.velocity );
	VectorSet( e->v.avelocity, 0.0f, 360.0f, 0.0f );
	// the mortars only aim at markers whose count (pev->impulse, seconds left)
	// is above 0: none while it flies, and none if it ends under a roof
	m = &marcadores[slot];
	m->cuenta = e->v.impulse;
	e->v.impulse = 0;

	e->v.movetype = MOVETYPE_BOUNCE;
	e->v.friction = 0.8f; // the bounces lose most of their speed
	ClearBits( e->v.flags, FL_ONGROUND );
	SV_LinkEdict( e, false );

	m->entidad = NUM_FOR_EDICT( e );
	m->vz = e->v.velocity[2];
	m->suelo = false;
	m->rodando = false;
	m->giro = 0.0f;
	m->hora = sv.time;
	m->flags = flags;
	m->eventindex = eventindex;
	if( angles ) VectorCopy( angles, m->angles );
	else VectorClear( m->angles );
	m->fparam1 = fparam1;
	m->fparam2 = fparam2;
	m->iparam1 = iparam1;
	m->iparam2 = iparam2;
	m->bparam1 = bparam1;
	m->bparam2 = bparam2;

	return true;
}

/*
==============
SV_MarcadorCielo

the sky is straight above it: the game asks the same of where the player
stands, but thrown it can end up under a roof, where the shells would come
through it
==============
*/
static qboolean SV_MarcadorCielo( edict_t *e )
{
	const char	*textura;
	vec3_t		desde, hasta;
	trace_t		tr;

	// as the game does it for the player (CBasePlayer, "must be placed
	// outside"): a line 6000 units up that ends on a sky texture
	VectorCopy( e->v.origin, desde );
	desde[2] += 16.0f;
	VectorCopy( desde, hasta );
	hasta[2] += 6000.0f;

	tr = SV_Move( desde, vec3_origin, vec3_origin, hasta, MOVE_NOMONSTERS, e, false );
	if( tr.fraction == 1.0f || !SV_IsValidEdict( tr.ent ) || NUM_FOR_EDICT( tr.ent ) != 0 )
		return false;

	textura = SV_TraceTexture( SV_EdictNum( 0 ), desde, hasta );
	return textura && !Q_strnicmp( textura, "sky", 3 );
}

/*
==============
SV_MarcadorSuelo

the ground under the rolling can, if it is there and not too steep to lie on
==============
*/
static qboolean SV_MarcadorSuelo( edict_t *e, float bajar, trace_t *tr )
{
	vec3_t	abajo;

	VectorCopy( e->v.origin, abajo );
	abajo[2] -= bajar;
	*tr = SV_Move( e->v.origin, (float *)rodar_mins, (float *)rodar_maxs, abajo, MOVE_NOMONSTERS, e, false );

	return !tr->allsolid && tr->fraction < 1.0f && tr->plane.normal[2] >= 0.5f;
}

/*
==============
SV_MarcadorRodar

the can on the ground, moved here and not by the engine's bounce (that stops
it dead on any slope when it goes slow): lying, it rolls down the slopes,
pulled by the gravity along them, and stops soon on flat ground. Off an edge,
or onto a slope too steep, it flies again. False when it lies still.
==============
*/
static qboolean SV_MarcadorRodar( marcador_t *m, edict_t *e )
{
	float	dt = sv.frametime, g = sv_gravity.value * ( e->v.gravity ? e->v.gravity : 1.0f );
	float	rapidez, caida, freno;
	vec3_t	n, tangente, hasta;
	trace_t	tr;

	if( !SV_MarcadorSuelo( e, 2.0f + VectorLength( e->v.velocity ) * dt, &tr ))
	{
		// in the air again, the engine's bounce
		m->rodando = false;
		e->v.movetype = MOVETYPE_BOUNCE;
		ClearBits( e->v.flags, FL_ONGROUND );
		return true;
	}

	VectorCopy( tr.endpos, e->v.origin );
	VectorCopy( tr.plane.normal, n );

	// the gravity along the slope (a rolling can takes about two thirds of it)
	VectorSet( tangente, 0.0f, 0.0f, -g );
	VectorMA( tangente, g * n[2], n, tangente );
	VectorScale( tangente, 0.7f * Q_max( sv_marcador_cuesta.value, 0.0f ), tangente );
	caida = VectorLength( tangente );
	VectorMA( e->v.velocity, dt, tangente, e->v.velocity );
	VectorMA( e->v.velocity, -DotProduct( e->v.velocity, n ), n, e->v.velocity );

	freno = n[2] > 0.985f ? MARCADOR_FRENO_LLANO : MARCADOR_FRENO_CUESTA;
	rapidez = VectorLength( e->v.velocity );
	// caught in a corner, rubbing on a wall that it can't get past
	if( VectorDistance( e->v.origin, m->paso ) > 4.0f )
	{
		VectorCopy( e->v.origin, m->paso );
		m->hora_paso = sv.time;
	}

	if(( rapidez <= freno * dt && caida <= freno ) || sv.time - m->hora_paso > 0.6 )
	{
		// it can't go on: it lies there
		VectorClear( e->v.velocity );
		SV_LinkEdict( e, false );
		return false;
	}
	if( rapidez > 0.0f )
		VectorScale( e->v.velocity, Q_max( rapidez - freno * dt, 0.0f ) / rapidez, e->v.velocity );

	// the move, bouncing a little off what it meets
	VectorMA( e->v.origin, dt, e->v.velocity, hasta );
	tr = SV_Move( e->v.origin, (float *)rodar_mins, (float *)rodar_maxs, hasta, MOVE_NOMONSTERS, e, false );
	if( !tr.allsolid )
		VectorCopy( tr.endpos, e->v.origin );
	if( tr.fraction < 1.0f )
	{
		float d = DotProduct( e->v.velocity, tr.plane.normal );

		if( d < 0.0f )
			VectorMA( e->v.velocity, -1.3f * d, tr.plane.normal, e->v.velocity );
	}

	// lying (the model is, with its axis along x), the axis across where it goes, turning about it
	rapidez = sqrt( e->v.velocity[0] * e->v.velocity[0] + e->v.velocity[1] * e->v.velocity[1] );
	if( rapidez > 1.0f )
		e->v.angles[YAW] = anglemod( RAD2DEG( atan2( e->v.velocity[1], e->v.velocity[0] )) + 90.0f );
	m->giro = anglemod( m->giro + RAD2DEG( rapidez * dt / MARCADOR_RADIO ));
	e->v.angles[PITCH] = 0.0f;
	e->v.angles[ROLL] = m->giro;

	SV_LinkEdict( e, false );
	return true;
}

/*
==============
SV_MarcadorFrame

after the game frame: the thrown markers roll to a stop, and once they lie
still the smoke is sent from there
==============
*/
void SV_MarcadorFrame( void )
{
	int	i;

	for( i = 0; i < svs.maxclients; i++ )
	{
		marcador_t	*m = &marcadores[i];
		edict_t		*e, *jugador;
		vec3_t		punto;
		qboolean	quieto;

		if( !m->entidad )
			continue;

		e = SV_EdictNum( m->entidad );
		jugador = SV_EdictNum( i + 1 );

		if( !SV_IsValidEdict( e ) || Q_strcmp( SV_GetString( e->v.classname ), "marker" ) || !SV_IsValidEdict( jugador ))
		{
			m->entidad = 0;
			continue;
		}

		quieto = false;

		if( !m->rodando )
		{
			trace_t	tr;

			// a bounce, or landing: a can doesn't skid far, it loses most of its speed
			if(( m->vz < 0.0f && e->v.velocity[2] >= 0.0f ) || ( FBitSet( e->v.flags, FL_ONGROUND ) && !m->suelo ))
			{
				if( !m->suelo )
					VectorCopy( e->v.origin, m->toque );
				m->suelo = true;
				e->v.velocity[0] *= 0.55f;
				e->v.velocity[1] *= 0.55f;
			}
			m->vz = e->v.velocity[2];

			// on the ground and no longer bouncing: it lies down and rolls
			if(( FBitSet( e->v.flags, FL_ONGROUND ) || fabs( e->v.velocity[2] ) < 60.0f ) && SV_MarcadorSuelo( e, 2.0f, &tr ))
			{
				if( !m->suelo )
					VectorCopy( e->v.origin, m->toque );
				m->suelo = true;
				m->rodando = true;
				VectorCopy( e->v.origin, m->paso );
				m->hora_paso = sv.time;
				e->v.movetype = MOVETYPE_NONE; // the game's think still runs
				VectorClear( e->v.avelocity );
				SV_SetMinMaxSize( e, rodar_mins, rodar_maxs, false );
			}
		}

		if( m->rodando )
			quieto = !SV_MarcadorRodar( m, e );

		if( quieto || sv.time - m->hora > 12.0 )
		{
			e->v.movetype = MOVETYPE_TOSS;
			VectorClear( e->v.avelocity );
			VectorClear( e->v.velocity );

			// under a roof it smokes all the same, but the mortars don't see it
			if( SV_MarcadorCielo( e ))
				e->v.impulse = m->cuenta - (int)( sv.time - m->hora );
			else
			{
				sv_client_t *cl = SV_ClientFromEdict( jugador, true );

				e->v.impulse = 0;
				if( cl && !FBitSet( cl->flags, FCL_FAKECLIENT ))
				{
					MSG_BeginServerCmd( &cl->netchan.message, svc_centerprint );
					MSG_WriteString( &cl->netchan.message, "El marcador ha caido a cubierto:\nla artilleria no lo ve" );
				}
			}

			Con_Reportf( "marcador: parado en %.0f %.0f %.0f, %s, cuenta %i\n", e->v.origin[0], e->v.origin[1], e->v.origin[2],
				e->v.impulse > 0 ? "al aire libre" : "a cubierto", e->v.impulse );
			Con_Reportf( "marcador: rodo %.0f unidades desde que toco el suelo, en %.1f s\n",
				m->suelo ? VectorDistance( m->toque, e->v.origin ) : 0.0f, sv.time - m->hora );

			// the clients put the smoke 10 units under the point
			VectorCopy( e->v.origin, punto );
			punto[2] += 16.0f;

			soltando = true;
			SV_PlaybackEventFull( m->flags, jugador, m->eventindex, 0.0f, punto, m->angles,
				m->fparam1, m->fparam2, m->iparam1, m->iparam2, m->bparam1, m->bparam2 );
			soltando = false;
			m->entidad = 0;
		}
	}
}
