/*
cl_ads.c - aiming down the sights
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

/*
Holding the secondary attack button with a weapon listed in <game>/ads.txt
brings its sights to the middle of the screen: the view zooms in a little,
smoothly, the weapon model moves so the sights line up with the centre and the
mouse slows down with the zoom. Only what is seen: the game doesn't know, the
accuracy is still the server's.

The button doesn't reach the game for those weapons (its secondary attack, the
bayonet of the AK-47, goes to +bayoneta, the middle mouse button).

ads.txt, one weapon per line:
  <view model> <fov> <forward> <right> <up>
the offsets move the model along the view, in units, at full aim. With the
bipod down the model is somewhere else: those aims are <view model>:bipode.
ads_ajuste changes them for the weapon in hand and ads_guardar writes them.
*/

static CVAR_DEFINE_AUTO( cl_ads, "1", FCVAR_ARCHIVE, "custom: aim down the sights with the secondary attack button, for the weapons in ads.txt" );
static CVAR_DEFINE_AUTO( cl_ads_tiempo, "0.15", FCVAR_ARCHIVE, "custom: seconds to bring the sights up" );
static CVAR_DEFINE_AUTO( cl_ads_sacudida_grados, "0.3", FCVAR_ARCHIVE, "custom: degrees the weapon jumps with each shot while aiming, at most (it turns about the eye: the sights stay lined up)" );
static CVAR_DEFINE_AUTO( cl_ads_valores, "1", FCVAR_ARCHIVE, "custom: while aiming, the fov, move and turn of the weapon in hand, on the top right (as ads_ajuste, ads_girar and ads_mover take them)" );
static CVAR_DEFINE_AUTO( cl_ads_sin_humo, "v_g3a3", FCVAR_ARCHIVE, "custom: with these weapons in hand (parts of their view model names, spaces between) the smoke and debris of the bullet impacts of Firearms aren't drawn: they covered the hole of the sights" );
static CVAR_DEFINE_AUTO( cl_ads_sacudida_tiempo, "0.08", FCVAR_ARCHIVE, "custom: seconds the weapon takes to jump and come back with each shot" );
static CVAR_DEFINE_AUTO( cl_ads_retroceso, "0.6", FCVAR_ARCHIVE, "custom: while aiming, how far each shot of Firearms 3.0 kicks the view (up and to the sides), times the normal" );
static CVAR_DEFINE_AUTO( cl_retroceso, "2", FCVAR_ARCHIVE, "custom: how far each shot of Firearms 3.0 kicks the view, aiming or not, times the game's" );
static CVAR_DEFINE_AUTO( cl_ads_retroceso_lateral, "1", FCVAR_ARCHIVE, "custom: while aiming, how far each shot kicks the view to the sides, times what cl_ads_retroceso leaves" );
static CVAR_DEFINE_AUTO( cl_ads_editar_paso, "0.02", FCVAR_ARCHIVE, "custom: with ads_editar, how far the weapon goes for each degree the mouse would turn the view" );

#define MAX_ADS	64

typedef struct
{
	char	modelo[72];	// <view model>, or <view model>:bipode
	float	fov;
	vec3_t	desplaza;	// forward, right, up
	vec3_t	gira;		// pitch, yaw, roll, added to the model's
	qboolean	nueva;		// not in ads.txt and not adjusted yet: ads_guardar leaves it out
} ads_t;

#define ADS_FOV_NUEVA	65.0f	// the aim of a weapon not in ads.txt: this zoom, the model where it is

// what is not aimed: knives, grenades, the claymore, the artillery marker
static const char *ads_sin_mira[] = { "knife", "claymore", "conc", "flash", "frag", "humo", "stg24", "marker" };

static ads_t	ads[MAX_ADS];
static int	ads_num;
static qboolean	ads_cargado;
static qboolean	ads_boton;	// the secondary attack button held
static qboolean	bayoneta;	// +bayoneta held
static qboolean	ads_fijo;	// ads_fijo: aiming without the button, to adjust it
static qboolean	ads_editar;	// ads_editar: the mouse places the weapon (CL_AdsEditar)
static qboolean	ads_sin_cruz;	// Q in ads_editar: the crosshair hidden
static float	ads_lupa = 1.0f;	// W and E in ads_editar: the view made bigger (r_lupa)
static qboolean	ads_tecla[4];	// Q, W, E, P: the press was taken, so is the release
static float	ads_frac;	// 0 = from the hip, 1 = aiming
static ads_t	*ads_arma;	// the weapon of the last frame aiming
static int	sacudida_dir = -1;	// of the last shot, one of the 8 around
static float	sacudida_amp;
static double	sacudida_hora;

static void CL_AdsCargar( void )
{
	char	*afile, *linea, *siguiente;

	ads_num = 0;
	ads_cargado = true;

	afile = (char *)FS_LoadFile( "ads.txt", NULL, false );
	if( !afile )
		return;

	// a line each, the turn at the end can be missing
	for( linea = afile; linea && *linea && ads_num < MAX_ADS; linea = siguiente )
	{
		ads_t	*a = &ads[ads_num];

		siguiente = Q_strchr( linea, '\n' );
		if( siguiente )
			*siguiente++ = '\0';

		memset( a, 0, sizeof( *a ));
		if( sscanf( linea, "%71s %f %f %f %f %f %f %f", a->modelo, &a->fov,
			&a->desplaza[0], &a->desplaza[1], &a->desplaza[2], &a->gira[0], &a->gira[1], &a->gira[2] ) < 5 )
			continue;

		if( a->modelo[0] != '/' && a->fov > 0.0f )
			ads_num++;
	}

	Mem_Free( afile );
}

static void CL_AdsMostrar( const ads_t *a )
{
	Con_Printf( "%s\n", a->modelo );
	Con_Printf( "ads_ajuste %g %g %g %g\n", a->fov, a->desplaza[0], a->desplaza[1], a->desplaza[2] );
	Con_Printf( "ads_girar total: %g %g %g\n", a->desplaza[0], a->desplaza[1], a->desplaza[2] );
	Con_Printf( "ads_mover total: %g %g %g\n", a->gira[0], a->gira[1], a->gira[2] );
}

/*
==============
CL_AdsValores

while aiming, what the weapon in hand has, on the top right (with the console allowed)
==============
*/
static void CL_AdsValores( const ads_t *a )
{
	con_nprint_t	info = { 0 };

	info.time_to_live = 0.2f;
	info.color[0] = info.color[1] = info.color[2] = 1.0f;

	info.index = 20;
	Con_NXPrintf( &info, "%s", a->modelo );
	info.index = 21;
	Con_NXPrintf( &info, "fov %g  ads_girar %g %g %g", a->fov, a->desplaza[0], a->desplaza[1], a->desplaza[2] );
	info.index = 22;
	Con_NXPrintf( &info, "ads_mover %g %g %g", a->gira[0], a->gira[1], a->gira[2] );
}

/*
==============
CL_AdsBipode

the bipod of the weapon in hand is down. Firearms' view models have the
animations of the bipod as bp_*, deploy_bipod or bipod_deploy, and the folding
with fold in them. The Barrett has those without it as ud_* and fires with the
bipod with plain fire1: with a model like that what isn't marked leaves it as
it was
==============
*/
static qboolean CL_AdsBipode( void )
{
	static qboolean	bipode;
	static model_t	*modelo;
	cl_entity_t	*view = &clgame.viewent;
	studiohdr_t	*hdr;
	mstudioseqdesc_t	*seq;
	const char	*label;
	int		i;

	if( view->model != modelo )
	{
		modelo = view->model;
		bipode = false;
	}

	if( !view->model || view->model->type != mod_studio )
		return false;

	hdr = (studiohdr_t *)Mod_StudioExtradata( view->model );
	if( !hdr || cl.local.weaponsequence < 0 || cl.local.weaponsequence >= hdr->numseq )
		return bipode;

	seq = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex );
	label = seq[cl.local.weaponsequence].label;

	if( Q_stristr( label, "fold" ) || !Q_strnicmp( label, "ud_", 3 ))
		bipode = false;
	else if( !Q_strnicmp( label, "bp_", 3 ) || Q_stristr( label, "bipod" ))
		bipode = true;
	else
	{
		for( i = 0; i < hdr->numseq && Q_strnicmp( seq[i].label, "ud_", 3 ); i++ );
		if( i == hdr->numseq )
			bipode = false;
	}

	return bipode;
}

static ads_t *CL_AdsArma( void )
{
	model_t	*mod;
	char	nombre[72];
	int	i;

	if( !ads_cargado )
		CL_AdsCargar();

	if( !ads_num || !cl_ads.value )
		return NULL;

	mod = CL_ModelHandle( CL_VariantesViewModel( cl.local.viewmodel ));
	if( !mod )
		return NULL;

	Q_snprintf( nombre, sizeof( nombre ), "%s%s", mod->name, CL_AdsBipode( ) ? ":bipode" : "" );
	for( i = 0; i < ads_num; i++ )
	{
		if( !Q_stricmp( ads[i].modelo, nombre ))
			return &ads[i];
	}

	// any other weapon aims too, with ADS_FOV_NUEVA, until it is adjusted
	if( mod->type != mod_studio || ads_num >= MAX_ADS || !Q_stristr( mod->name, "/v_" ))
		return NULL;
	for( i = 0; i < ARRAYSIZE( ads_sin_mira ); i++ )
	{
		if( Q_stristr( COM_FileWithoutPath( mod->name ), ads_sin_mira[i] ))
			return NULL;
	}

	memset( &ads[ads_num], 0, sizeof( ads[ads_num] ));
	ads[ads_num].fov = ADS_FOV_NUEVA;

	// with the bipod down, from the aim without it
	for( i = 0; i < ads_num; i++ )
	{
		if( !Q_stricmp( ads[i].modelo, mod->name ))
			ads[ads_num] = ads[i];
	}

	Q_strncpy( ads[ads_num].modelo, nombre, sizeof( ads[ads_num].modelo ));
	ads[ads_num].nueva = true;
	return &ads[ads_num++];
}

/*
==============
CL_AdsEditar

ads_editar: the mouse doesn't turn the view, it places the weapon in hand,
and the weapon doesn't fire. Alone it moves the weapon (the turn of the
model, as ads_mover), with the left button held it turns the sights (the
place of the model, as ads_girar), with the right one held it comes nearer
or goes away (up and down)
==============
*/
static void CL_AdsEditar( ads_t *a, usercmd_t *cmd, const vec3_t angulos_antes )
{
	float	paso = cl_ads_editar_paso.value;
	float	arriba = -( cl.viewangles[PITCH] - angulos_antes[PITCH] ) * paso;	// the mouse up
	float	izquierda = cl.viewangles[YAW] - angulos_antes[YAW];

	if( izquierda > 180.0f ) izquierda -= 360.0f;
	else if( izquierda < -180.0f ) izquierda += 360.0f;
	izquierda *= paso;

	if( FBitSet( cmd->buttons, IN_ATTACK2 ))
	{
		a->desplaza[0] += arriba;
		izquierda = 0.0f;
	}
	else if( FBitSet( cmd->buttons, IN_ATTACK ))
	{
		a->desplaza[1] -= izquierda;
		a->desplaza[2] += arriba;
	}
	else
	{
		a->gira[PITCH] += arriba;
		a->gira[YAW] += izquierda;
	}

	if( arriba != 0.0f || izquierda != 0.0f )
		a->nueva = false;

	// the view stays where it was
	cl.viewangles[PITCH] = angulos_antes[PITCH];
	cl.viewangles[YAW] = angulos_antes[YAW];
	VectorCopy( cl.viewangles, cmd->viewangles );
	ClearBits( cmd->buttons, IN_ATTACK|IN_ATTACK2 );
	ads_boton = false;
}

/*
==============
CL_AdsCmd

after client.dll made the command: the button is the aim with these weapons,
+bayoneta is the secondary attack. The mouse is slowed down with the zoom
==============
*/
static float CL_AdsSuave( void );

void CL_AdsCmd( usercmd_t *cmd, const vec3_t angulos_antes, qboolean active )
{
	ads_t	*a = CL_AdsArma();

	ads_boton = false;

	if( a && ads_editar && active )
	{
		CL_AdsEditar( a, cmd, angulos_antes );
		return;
	}

	if( a )
	{
		ads_boton = active && FBitSet( cmd->buttons, IN_ATTACK2 );
		ClearBits( cmd->buttons, IN_ATTACK2 );
	}

	if( bayoneta && active )
		SetBits( cmd->buttons, IN_ATTACK2 );

	if( ads_frac > 0.0f && ads_arma && active )
	{
		float	base = cl.local.scr_fov > 0.0f ? cl.local.scr_fov : 90.0f;
		float	fov = base + ( ads_arma->fov - base ) * ads_frac;
		float	escala = tan( DEG2RAD( fov * 0.5f )) / tan( DEG2RAD( base * 0.5f ));
		int	i;

		// only the turn of this frame, the mouse
		for( i = 0; i < 2; i++ )
		{
			float delta = cl.viewangles[i] - angulos_antes[i];

			if( delta > 180.0f ) delta -= 360.0f;
			else if( delta < -180.0f ) delta += 360.0f;
			cl.viewangles[i] = angulos_antes[i] + delta * escala;
		}

		VectorCopy( cl.viewangles, cmd->viewangles );
	}

	// the sway of the aim, with the realistic precision (cl_vaiven.c)
	if( active )
	{
		model_t *mod = CL_ModelHandle( CL_VariantesViewModel( cl.local.viewmodel ));

		CL_VaivenAplicar( cmd, ads_arma && ads_frac > 0.0f ? CL_AdsSuave() : 0.0f, mod ? mod->name : NULL, CL_AdsBipode( ));
	}
}

/*
==============
CL_AdsRecargando

the weapon model is playing a reload, until its last frame (client.dll can
leave it there a while before the idle)
==============
*/
static qboolean CL_AdsRecargando( void )
{
	cl_entity_t	*view = &clgame.viewent;
	studiohdr_t	*hdr;
	mstudioseqdesc_t	*seq;

	if( !view->model || view->model->type != mod_studio )
		return false;

	hdr = (studiohdr_t *)Mod_StudioExtradata( view->model );
	if( !hdr || cl.local.weaponsequence < 0 || cl.local.weaponsequence >= hdr->numseq )
		return false;

	seq = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex ) + cl.local.weaponsequence;
	if( !Q_stristr( seq->label, "reload" ))
		return false;

	return ( cl.time - cl.local.weaponstarttime ) * seq->fps < seq->numframes - 1;
}

/*
==============
CL_AdsSinTransicion

with ads_editar the bipod coming down or being folded isn't played: the
weapon is in its last frame at once, to see where it is left right away
==============
*/
static void CL_AdsSinTransicion( void )
{
	cl_entity_t	*view = &clgame.viewent;
	studiohdr_t	*hdr;
	const char	*label;

	if( !view->model || view->model->type != mod_studio )
		return;

	hdr = (studiohdr_t *)Mod_StudioExtradata( view->model );
	if( !hdr || cl.local.weaponsequence < 0 || cl.local.weaponsequence >= hdr->numseq )
		return;

	label = ((mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex ) + cl.local.weaponsequence )->label;
	if( !Q_stristr( label, "fold" ) && !( Q_stristr( label, "bipod" ) && Q_strnicmp( label, "bp_", 3 )))
		return;

	// long gone: the studio code leaves it in the last frame
	cl.local.weaponstarttime = cl.time - 60.0f;
	view->curstate.animtime = cl.local.weaponstarttime;
}

/*
==============
CL_AdsAvisar

the server is told when the aim is up or down: it shoots closer to the view
then (ads_bala.c, sv_ads_dispersion)
==============
*/
static qboolean	ads_avisado;	// what the server was told
static int	ads_servidor = -1;	// the cl.servercount it was told in

static void CL_AdsAvisar( qboolean apuntando )
{
	if( cls.state != ca_active || cls.demoplayback )
		return;

	if( ads_servidor != cl.servercount )
	{
		ads_servidor = cl.servercount;
		ads_avisado = false;
	}

	if( apuntando != ads_avisado )
	{
		CL_ServerCommand( true, "ads %d", apuntando );
		ads_avisado = apuntando;
	}
}

/*
==============
CL_AdsBala

the traces of the events of the local player's shots, while aiming or with a
weapon variant: the impacts drawn where the server's bullets go (ads_bala.c)
==============
*/
static const event_args_t	*ads_evento;	// the event being played

void CL_AdsEvento( const event_args_t *args )
{
	ads_evento = args;
}

const float *CL_AdsBala( const float *v1, const float *v2, vec3_t fin )
{
	const event_args_t	*a = ads_evento;
	float		k, moa;

	if( !a || a->entindex != cl.playernum + 1 )
		return v2;

	k = CL_VariantesPrecision( a->entindex );
	moa = 0.0f;
	if( ads_avisado && CL_PrecisionRealista( ))
	{
		// where the sights are, off only by what the weapon does (as sv_game.c)
		model_t *mod = CL_ModelHandle( CL_VariantesViewModel( cl.local.viewmodel ));

		COM_ArmaReal( mod ? mod->name : NULL, &moa );
		if( moa >= 0.0f )
		{
			moa *= k;
			k = 0.0f;
		}
		else
		{
			moa = 0.0f;
			k *= Cvar_VariableValue( "sv_ads_dispersion" );
		}
	}
	else if( ads_avisado )
		k *= Cvar_VariableValue( "sv_ads_dispersion" );
	else if( CL_PrecisionRealista( ))
		k *= Cvar_VariableValue( "precision_cadera" );

	// from the eye the start is the eye, the trace through a wall goes by its end
	return COM_AdsBala( 1, a, cl.time, VectorDistance( v1, a->origin ) < 64.0f ? v1 : a->origin,
		a->angles, k, moa, v1, v2, fin );
}

/*
==============
CL_AdsRetroceso

Firearms 3.0's client.dll kicks the view with each shot of the local player
(the function at FA_RETROCESO: up, and to a side at random, less with the
bipod, prone or crouched) and sets it with SetViewAngles. That change is
scaled here: by cl_retroceso, by the recoil of the variant in hand
(variantes.txt, retroceso and lateral) and by cl_ads_retroceso (cl_ads_retroceso_lateral too for the
sides) while aiming
==============
*/
#define FA_RETROCESO		0x44660	// the kick, in client.dll
#define FA_RETROCESO_RETORNO	0x4479d	// after its call to SetViewAngles

void CL_AdsRetroceso( float *angles, const void *retorno )
{
	static const byte	inicio[] = { 0x83, 0xec, 0x14, 0x8d, 0x4c, 0x24, 0x08 };
	static const byte	*cliente;
	static qboolean	mirado;
	float		k, lateral, d;

	if( !mirado )
	{
		mirado = true;
#if XASH_WIN32
		cliente = (const byte *)GetModuleHandleA( "client.dll" );
		// another client.dll: nothing done
		if( cliente && ( memcmp( cliente + FA_RETROCESO, inicio, sizeof( inicio ))
			|| cliente[FA_RETROCESO_RETORNO - 6] != 0xff || cliente[FA_RETROCESO_RETORNO - 5] != 0x15 ))
			cliente = NULL;
#endif
	}

	if( !cliente || (const byte *)retorno != cliente + FA_RETROCESO_RETORNO )
		return;

	CL_VaivenDisparo(); // the shot shakes the sway too

	k = CL_VariantesRetroceso( cl.playernum + 1, &lateral ) * cl_retroceso.value;
	if( ads_avisado )
	{
		k *= cl_ads_retroceso.value;
		lateral *= cl_ads_retroceso_lateral.value;
	}

	d = angles[1] - cl.viewangles[1];
	if( d > 180.0f )
		d -= 360.0f;
	else if( d < -180.0f )
		d += 360.0f;

	if( Cvar_VariableValue( "variantes_debug" ) >= 2.0f )
		Con_Printf( "[retroceso] %g %g -> %g %g\n", angles[0] - cl.viewangles[0], d, ( angles[0] - cl.viewangles[0] ) * k, d * k * lateral );

	angles[0] = cl.viewangles[0] + ( angles[0] - cl.viewangles[0] ) * k;
	angles[1] = cl.viewangles[1] + d * k * lateral;
}

/*
==============
CL_AdsFrame

the aim comes up or goes down, every frame
==============
*/
void CL_AdsFrame( void )
{
	ads_t	*a = CL_AdsArma();
	float	paso = cl_ads_tiempo.value > 0.0f ? host.frametime / cl_ads_tiempo.value : 1.0f;
	qboolean	apuntando = a && ( ads_boton || ads_fijo || ads_editar ) && cl.local.health > 0 && !cls.demoplayback && !CL_AdsRecargando() && !CL_MarcadorActivo();

	if( a )
		ads_arma = a;

	if( ads_editar )
		CL_AdsSinTransicion();

	CL_AdsAvisar( apuntando );

	if( apuntando && cl_ads_valores.value )
		CL_AdsValores( a );

	if( apuntando )
		ads_frac = Q_min( ads_frac + paso, 1.0f );
	else ads_frac = Q_max( ads_frac - paso, 0.0f );

	// another weapon in the hand: straight down
	if( !a )
		ads_frac = 0.0f;
}

static float CL_AdsSuave( void )
{
	// eased in and out
	return ads_frac * ads_frac * ( 3.0f - 2.0f * ads_frac );
}

/*
==============
CL_AdsFov

the final fov of the view
==============
*/
float CL_AdsFov( float fov )
{
	float	f = CL_AdsSuave();

	if( f <= 0.0f || !ads_arma || ads_arma->fov >= fov )
		return fov;

	return fov + ( ads_arma->fov - fov ) * f;
}

qboolean CL_AdsApuntando( void )
{
	return ads_frac > 0.0f;
}

/*
==============
CL_AdsViewModel

after V_CalcRefdef placed the weapon model
==============
*/
static void CL_AdsQuieto( void );

void CL_AdsViewModel( const ref_params_t *rp )
{
	static float	signo = -1.0f; // the model's pitch against the view's (HL SDK V_CalcGunAngle: -pitch)
	cl_entity_t	*view = &clgame.viewent;
	float	f = CL_AdsSuave();
	vec3_t	origen, angulos, vista, adelante, derecha, arriba;
	int	i;

	// the artillery marker is in the hand (cl_marcador.c)
	if( CL_MarcadorActivo( ))
		return;

	CL_AdsQuieto();

	// learnt from client.dll's own, while it is far from level
	if( fabs( rp->viewangles[PITCH] ) > 15.0f && fabs( view->angles[PITCH] ) > 5.0f )
		signo = ( view->angles[PITCH] * rp->viewangles[PITCH] < 0.0f ) ? -1.0f : 1.0f;

	if( f <= 0.0f || !ads_arma )
		return;

	// the shot: out to its side fast, back home slower. The weapon turns about
	// the eye, so the rear and the front sights stay in line and only where
	// they point jumps (moved, the near one went further than the far one)
	VectorCopy( rp->viewangles, vista );
	if( sacudida_dir >= 0 && cl_ads_sacudida_tiempo.value > 0.0f )
	{
		float t = ( cl.time - sacudida_hora ) / cl_ads_sacudida_tiempo.value;

		if( t >= 0.0f && t < 1.0f )
		{
			float k = t < 0.25f ? t / 0.25f : ( 1.0f - t ) / 0.75f;
			float a = DEG2RAD( sacudida_dir * 45.0f );

			vista[YAW] -= cos( a ) * sacudida_amp * k;
			vista[PITCH] -= sin( a ) * sacudida_amp * k;
		}
	}
	AngleVectors( vista, adelante, derecha, arriba );

	// aiming, the weapon is held still in front of the eye: not where client.dll
	// puts it, that tilts it when looking up or down, bobs it and lags it.
	// The unit down is client.dll's own with the view level, where ads.txt was made
	VectorMA( rp->vieworg, -1.0f, arriba, origen );
	VectorMA( origen, ads_arma->desplaza[0], adelante, origen );
	VectorMA( origen, ads_arma->desplaza[1], derecha, origen );
	VectorMA( origen, ads_arma->desplaza[2], arriba, origen );

	angulos[PITCH] = signo * vista[PITCH];
	angulos[YAW] = vista[YAW];
	angulos[ROLL] = vista[ROLL];
	VectorAdd( angulos, ads_arma->gira, angulos );

	for( i = 0; i < 3; i++ )
	{
		float delta = angulos[i] - view->angles[i];

		while( delta > 180.0f ) delta -= 360.0f;
		while( delta < -180.0f ) delta += 360.0f;

		view->origin[i] += ( origen[i] - view->origin[i] ) * f;
		view->angles[i] += delta * f;
	}

	// client.dll's studio renderer turns it with curstate.angles (HL SDK StudioSetUpTransform)
	VectorCopy( view->angles, view->curstate.angles );
}

/*
==============
CL_AdsReposo

the idle shown instead of a firing animation: the first one of its kind
(nmc_fire1 -> nmc_idle1)
==============
*/
static int CL_AdsReposo( const studiohdr_t *hdr, const char *label, int prefijo )
{
	mstudioseqdesc_t	*seq = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex );
	int		i;

	for( i = 0; i < hdr->numseq; i++ )
	{
		if( !Q_strnicmp( seq[i].label, label, prefijo ) && !Q_strnicmp( seq[i].label + prefijo, "idle", 4 ))
			return i;
	}

	return -1;
}

/*
==============
CL_AdsDisparo

a shot while aiming: the weapon jumps to one of the 8 sides around, never the
same twice in a row, and the events of the start of the animation that isn't
played (the muzzle flash) are sent to client.dll by hand
==============
*/
static void CL_AdsDisparo( const studiohdr_t *hdr, const mstudioseqdesc_t *seq, cl_entity_t *view )
{
	mstudioevent_t	*ev = (mstudioevent_t *)((byte *)hdr + seq->eventindex );
	int		i, dir;

	do dir = COM_RandomLong( 0, 7 );
	while( dir == sacudida_dir );

	sacudida_dir = dir;
	sacudida_amp = cl_ads_sacudida_grados.value * COM_RandomFloat( 0.5f, 1.0f );
	sacudida_hora = cl.time;

	if( !clgame.dllFuncs.pfnStudioEvent )
		return;

	for( i = 0; i < seq->numevents; i++ )
	{
		if( ev[i].event >= 5000 /* client side */ && ev[i].frame <= 0 )
			clgame.dllFuncs.pfnStudioEvent( &ev[i], view );
	}
}

/*
==============
CL_AdsQuieto

aiming, the idle animation of the weapon goes to its first frame and stays
there: it moved the sights, and stopping it where it was left the weapon
somewhere else each time. It gets there with the aim coming up, and starts
again from it when the aim is let go. The firing animations aren't played
either, the weapon stays in that pose and only jumps a little (CL_AdsDisparo).
Reloading and the rest still play
==============
*/
static void CL_AdsQuieto( void )
{
	static qboolean	parado;
	static float	framerate = 1.0f;	// the one before stopping it
	static int	sequence = -1;
	static float	animtime;
	static float	inicio;		// the frame it was in when stopped
	static float	pico;		// the most the aim came up since
	static float	disparo = -1.0f;	// the animtime of the last shot seen
	cl_entity_t	*view = &clgame.viewent;
	studiohdr_t	*hdr = NULL;
	mstudioseqdesc_t	*seq = NULL;
	const char	*fuego = NULL;
	qboolean	reposo = false;

	if( view->model && view->model->type == mod_studio )
		hdr = (studiohdr_t *)Mod_StudioExtradata( view->model );

	if( hdr && view->curstate.sequence >= 0 && view->curstate.sequence < hdr->numseq )
	{
		seq = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex ) + view->curstate.sequence;
		reposo = Q_stristr( seq->label, "idle" ) != NULL;
		fuego = Q_stristr( seq->label, "fire" );
	}

	if( ads_frac > 0.0f && fuego )
	{
		int quieto = CL_AdsReposo( hdr, seq->label, fuego - seq->label );

		if( view->curstate.animtime != disparo )
		{
			disparo = view->curstate.animtime;
			CL_AdsDisparo( hdr, seq, view );
		}

		if( !parado )
			framerate = view->curstate.framerate ? view->curstate.framerate : 1.0f;
		parado = true;
		sequence = -1;
		pico = 1.0f;
		view->curstate.framerate = 0.0f;
		view->curstate.frame = 0.0f;
		if( quieto >= 0 )
			view->curstate.sequence = quieto;
		return;
	}

	if( ads_frac > 0.0f && reposo )
	{
		// a new idle (after firing, say): from the frame it is in now
		if( sequence != view->curstate.sequence || animtime != view->curstate.animtime )
		{
			float rate = parado ? framerate : ( view->curstate.framerate ? view->curstate.framerate : 1.0f );

			sequence = view->curstate.sequence;
			animtime = view->curstate.animtime;
			inicio = view->curstate.frame + ( cl.time - animtime ) * rate * seq->fps;
			if( seq->numframes > 1 )
			{
				if( FBitSet( seq->flags, STUDIO_LOOPING ))
					inicio = fmod( inicio, seq->numframes - 1 );
				else inicio = Q_min( inicio, seq->numframes - 1 );
			}
			pico = 0.0f;
		}

		if( !parado )
			framerate = view->curstate.framerate ? view->curstate.framerate : 1.0f;
		parado = true;
		pico = Q_max( pico, CL_AdsSuave( ));
		view->curstate.framerate = 0.0f;
		view->curstate.frame = inicio * ( 1.0f - pico );
	}
	else if( parado )
	{
		parado = false;
		sequence = -1;
		view->curstate.framerate = framerate;

		// it was left at the first frame: the idle starts again from there
		if( reposo && pico >= 1.0f )
		{
			cl.local.weaponstarttime = cl.time;
			view->curstate.animtime = cl.time;
			view->curstate.frame = 0.0f;
		}
	}
}

static void CL_Bayoneta_Down( void ) { bayoneta = true; }
static void CL_Bayoneta_Up( void ) { bayoneta = false; }

/*
==============
CL_AdsAjuste_f

ads_ajuste <fov> <forward> <right> <up>, for the weapon in hand
==============
*/
static void CL_AdsAjuste_f( void )
{
	ads_t	*a = CL_AdsArma();
	model_t	*mod;
	int	i;

	if( !a )
	{
		mod = CL_ModelHandle( CL_VariantesViewModel( cl.local.viewmodel ));

		if( !mod || Cmd_Argc() != 5 || ads_num >= MAX_ADS )
		{
			Con_Printf( "the weapon in hand has no aim: ads_ajuste <fov> <forward> <right> <up> to give it one\n" );
			return;
		}

		a = &ads[ads_num++];
		Q_strncpy( a->modelo, mod->name, sizeof( a->modelo ));
	}

	if( Cmd_Argc() != 5 )
	{
		CL_AdsMostrar( a );
		Con_Printf( "ads_ajuste <fov> <forward> <right> <up>\n" );
		return;
	}

	a->fov = Q_atof( Cmd_Argv( 1 ));
	for( i = 0; i < 3; i++ )
		a->desplaza[i] = Q_atof( Cmd_Argv( i + 2 ));
	a->nueva = false;
}

/*
==============
CL_AdsMover_f

ads_girar <forward> <right> <up>: adds to the aim of the weapon in hand.
It moves the model, but what is seen is the sights turning: the rear one,
near the eye, goes further than the front one (ads_girar and ads_mover are
named after what is seen)
==============
*/
static void CL_AdsMover_f( void )
{
	ads_t	*a = CL_AdsArma();
	int	i;

	if( !a || Cmd_Argc() != 4 )
	{
		Con_Printf( "ads_girar <forward> <right> <up>, with a weapon in hand\n" );
		return;
	}

	for( i = 0; i < 3; i++ )
		a->desplaza[i] += Q_atof( Cmd_Argv( i + 1 ));
	a->nueva = false;

	CL_AdsMostrar( a );
}

/*
==============
CL_AdsGirar_f

ads_mover <pitch> <yaw> <roll>: adds to the turn of the weapon in hand.
It turns the model about the eye: what is seen is the weapon moving
==============
*/
static void CL_AdsGirar_f( void )
{
	ads_t	*a = CL_AdsArma();
	int	i;

	if( !a || Cmd_Argc() != 4 )
	{
		Con_Printf( "ads_mover <pitch> <yaw> <roll>, with a weapon in hand\n" );
		return;
	}

	for( i = 0; i < 3; i++ )
		a->gira[i] += Q_atof( Cmd_Argv( i + 1 ));
	a->nueva = false;

	CL_AdsMostrar( a );
}

static void CL_AdsFijo_f( void )
{
	ads_fijo = !ads_fijo;
	Con_Printf( "aim %s\n", ads_fijo ? "kept up" : "with the button" );
}

static void CL_AdsGuardar_f( void );

/*
==============
CL_AdsEditar_f

the mouse places the weapon in hand while aiming (CL_AdsEditar), again to
leave it: what was placed is written to ads.txt then
==============
*/
static void CL_AdsEditar_f( void )
{
	ads_editar = !ads_editar;
	ads_lupa = 1.0f;
	Cvar_SetValue( "r_lupa", ads_lupa );

	if( ads_editar )
	{
		Con_Printf( "the mouse places the weapon: alone it moves it, with the left button it turns the sights, with the right one it comes nearer (up and down). Q the crosshair, W and E the magnifying glass, P back to the one saved; ads_editar again to save it\n" );
		return;
	}

	CL_AdsGuardar_f();
}

/*
==============
CL_AdsRestaurar

P in ads_editar: the weapon in hand (with the bipod down, its :bipode aim)
goes back to what ads.txt has for it, the last one saved
==============
*/
static void CL_AdsRestaurar( void )
{
	ads_t	*a = CL_AdsArma();
	char	*texto, *p, *sig;
	ads_t	leida;

	if( !a )
		return;

	texto = (char *)FS_LoadFile( "ads.txt", NULL, false );
	for( p = texto; p && *p; p = sig )
	{
		sig = Q_strchr( p, '\n' );
		if( sig )
			*sig++ = '\0';

		memset( &leida, 0, sizeof( leida ));
		if( sscanf( p, "%71s %f %f %f %f %f %f %f", leida.modelo, &leida.fov,
			&leida.desplaza[0], &leida.desplaza[1], &leida.desplaza[2], &leida.gira[0], &leida.gira[1], &leida.gira[2] ) < 5 )
			continue;
		if( Q_stricmp( leida.modelo, a->modelo ) || leida.fov <= 0.0f )
			continue;

		a->fov = leida.fov;
		VectorCopy( leida.desplaza, a->desplaza );
		VectorCopy( leida.gira, a->gira );
		a->nueva = false;
		Mem_Free( texto );
		Con_Printf( "%s: back to the one saved in ads.txt\n", a->modelo );
		return;
	}

	if( texto )
		Mem_Free( texto );
	Con_Printf( "%s: not saved in ads.txt yet\n", a->modelo );
}

/*
==============
CL_AdsTecla

with ads_editar, Q shows or hides the crosshair, W and E make the view
bigger and smaller (a magnifying glass, r_lupa: the fov stays) and P puts
back the aim last saved (CL_AdsRestaurar). The keys don't reach the game then
==============
*/
qboolean CL_AdsTecla( int key, qboolean down )
{
	static const int teclas[4] = { 'q', 'w', 'e', 'p' };
	int i;

	for( i = 0; i < 4 && key != teclas[i]; i++ );
	if( i == 4 )
		return false;

	if( !down )
	{
		// the release of a press it took
		qboolean tomada = ads_tecla[i];

		ads_tecla[i] = false;
		return tomada;
	}

	if( !ads_editar || cls.state != ca_active )
		return false;

	ads_tecla[i] = true;
	if( i == 3 )
	{
		CL_AdsRestaurar();
		return true;
	}

	if( i == 0 )
		ads_sin_cruz = !ads_sin_cruz;
	else if( i == 1 )
		ads_lupa = Q_min( ads_lupa * 1.5f, 16.0f );
	else ads_lupa = Q_max( ads_lupa / 1.5f, 1.0f );

	Cvar_SetValue( "r_lupa", ads_lupa );
	return true;
}

/*
==============
CL_AdsDraw

with ads_fijo on, a crosshair in the middle of the screen, always (Firearms has none):
to put the sights on it while adjusting them
==============
*/
void CL_AdsDraw( void )
{
	int	cx, cy, t, largo, hueco, i;

	if( !( ads_fijo || ( ads_editar && !ads_sin_cruz )) || cls.state != ca_active || cls.key_dest != key_game )
		return;

	t = Q_max( 1, (int)( refState.height / 1080.0f + 0.5f ));
	largo = 10 * t;
	hueco = 4 * t;
	cx = refState.width / 2;
	cy = refState.height / 2;

	// black under it, so it is seen on anything, then green
	for( i = 0; i < 2; i++ )
	{
		int	b = i ? 0 : 1;
		byte	r = i ? 60 : 0, g = i ? 255 : 0, bl = i ? 60 : 0, a = i ? 230 : 160;

		ref.dllFuncs.FillRGBA( kRenderTransTexture, cx - hueco - largo - b, cy - b, largo + b * 2, t + b * 2, r, g, bl, a );
		ref.dllFuncs.FillRGBA( kRenderTransTexture, cx + hueco + t - b, cy - b, largo + b * 2, t + b * 2, r, g, bl, a );
		ref.dllFuncs.FillRGBA( kRenderTransTexture, cx - b, cy - hueco - largo - b, t + b * 2, largo + b * 2, r, g, bl, a );
		ref.dllFuncs.FillRGBA( kRenderTransTexture, cx - b, cy + hueco + t - b, t + b * 2, largo + b * 2, r, g, bl, a );
		ref.dllFuncs.FillRGBA( kRenderTransTexture, cx - b, cy - b, t + b * 2, t + b * 2, r, g, bl, a );
	}
}

/*
==============
the smoke of the impacts

Firearms' client.dll draws its particles itself, with the triangle API. What
the bullets raise where they hit (the smoke, sprites/fasmoke1.spr, and the
debris and sand) stays seconds right where the sights aim: through the hole of
the G3A3's rear sight it covered the front one, as cl_particles 0 showed. With
the weapons of cl_ads_sin_humo in hand none of it is drawn
==============
*/
static const char *humo_sprites[] = { "sprites/fasmoke", "sprites/debris_", "sprites/newsmoke", "sprites/sand", "sprites/steam1", "sprites/stmbal1" };

static triangleapi_t	tri_real;
static qboolean	humo_sprite;	// the texture bound is one of humo_sprites
static qboolean	humo_saltar;	// this Begin .. End is left out

static qboolean CL_AdsSinHumo( void )
{
	model_t	*mod;
	char	lista[256], *p, *t;

	if( !cl_ads_sin_humo.string[0] || !( mod = CL_ModelHandle( CL_VariantesViewModel( cl.local.viewmodel ))))
		return false;

	Q_strncpy( lista, cl_ads_sin_humo.string, sizeof( lista ));
	for( p = lista; ( t = strtok( p, " ,;" )); p = NULL )
	{
		if( Q_stristr( mod->name, t ))
			return true;
	}
	return false;
}

static int CL_AdsTriSpriteTexture( model_t *spr, int frame )
{
	int	i;

	humo_sprite = false;
	for( i = 0; spr && i < ARRAYSIZE( humo_sprites ) && !humo_sprite; i++ )
		humo_sprite = !Q_strnicmp( spr->name, humo_sprites[i], Q_strlen( humo_sprites[i] ));
	return tri_real.SpriteTexture( spr, frame );
}

static void CL_AdsTriBegin( int modo )
{
	humo_saltar = humo_sprite && CL_AdsSinHumo( );
	if( !humo_saltar )
		tri_real.Begin( modo );
}

static void CL_AdsTriVertex3fv( const float *v )
{
	if( !humo_saltar )
		tri_real.Vertex3fv( v );
}

static void CL_AdsTriVertex3f( float x, float y, float z )
{
	if( !humo_saltar )
		tri_real.Vertex3f( x, y, z );
}

static void CL_AdsTriEnd( void )
{
	if( !humo_saltar )
		tri_real.End();
	humo_saltar = false;
}

void CL_AdsTriAPI( triangleapi_t *tri )
{
	tri_real = *tri;
	tri->SpriteTexture = CL_AdsTriSpriteTexture;
	tri->Begin = CL_AdsTriBegin;
	tri->End = CL_AdsTriEnd;
	tri->Vertex3f = CL_AdsTriVertex3f;
	tri->Vertex3fv = CL_AdsTriVertex3fv;
}

/*
==============
CL_AdsGuardar_f

writes the aim of the weapon in hand to ads.txt (with the bipod down, its
:bipode one): the line of that weapon is changed or added, the rest of the
file stays as it is, whatever was moved of the other weapons too
==============
*/
static void CL_AdsGuardar_f( void )
{
	ads_t	*a = CL_AdsArma();
	char	linea[256];
	char	*texto, *p, *sig;
	string	modelo;
	file_t	*f;
	qboolean	puesta = false;

	if( !a )
		a = ads_arma;
	if( !a )
	{
		Con_Printf( "ads_guardar: no weapon that aims in the hand\n" );
		return;
	}

	Q_snprintf( linea, sizeof( linea ), "%s %g %g %g %g %g %g %g\n", a->modelo, a->fov, a->desplaza[0], a->desplaza[1], a->desplaza[2], a->gira[0], a->gira[1], a->gira[2] );
	texto = (char *)FS_LoadFile( "ads.txt", NULL, true );

	f = FS_Open( "ads.txt", "w", true );
	if( !f )
	{
		Con_Printf( S_ERROR "can't write ads.txt\n" );
		if( texto )
			Mem_Free( texto );
		return;
	}

	if( !texto )
		FS_Printf( f, "// aim down the sights (cl_ads.c): <view model> <fov> <forward> <right> <up> <pitch> <yaw> <roll>\n" );

	for( p = texto; p && *p; p = sig )
	{
		size_t	largo;

		sig = Q_strchr( p, '\n' );
		sig = sig ? sig + 1 : p + Q_strlen( p );
		largo = sig - p;

		modelo[0] = '\0';
		sscanf( p, "%255s", modelo );
		if( !Q_stricmp( modelo, a->modelo ))
		{
			if( !puesta )
				FS_Printf( f, "%s", linea );
			puesta = true;
			continue;
		}

		// the file is written as text: its own end of line goes as \n
		while( largo > 0 && ( p[largo - 1] == '\n' || p[largo - 1] == '\r' ))
			largo--;
		FS_Write( f, p, largo );
		FS_Printf( f, "\n" );
	}

	if( !puesta )
		FS_Printf( f, "%s", linea );
	FS_Close( f );

	if( texto )
		Mem_Free( texto );

	a->nueva = false;
	Con_Printf( "ads.txt: %s", linea );
}

void CL_InitAds( void )
{
	Cvar_RegisterVariable( &cl_ads );
	Cvar_RegisterVariable( &cl_ads_tiempo );
	Cvar_RegisterVariable( &cl_ads_sacudida_grados );
	Cvar_RegisterVariable( &cl_ads_sacudida_tiempo );
	Cvar_RegisterVariable( &cl_ads_editar_paso );
	Cvar_RegisterVariable( &cl_ads_retroceso );
	Cvar_RegisterVariable( &cl_ads_retroceso_lateral );
	Cvar_RegisterVariable( &cl_retroceso );
	Cvar_RegisterVariable( &cl_ads_valores );
	Cvar_RegisterVariable( &cl_ads_sin_humo );
	Cmd_AddCommand( "+bayoneta", CL_Bayoneta_Down, "custom: secondary attack (the right button aims with the weapons that aim)" );
	Cmd_AddCommand( "-bayoneta", CL_Bayoneta_Up, "custom: secondary attack" );
	Cmd_AddCommand( "ads_ajuste", CL_AdsAjuste_f, "custom: <fov> <forward> <right> <up> of the aim of the weapon in hand" );
	Cmd_AddCommand( "ads_guardar", CL_AdsGuardar_f, "custom: writes the aim of the weapon in hand to ads.txt" );
	Cmd_AddCommand( "ads_girar", CL_AdsMover_f, "custom: <forward> <right> <up> added to the place of the weapon in hand while aiming (seen as the sights turning)" );
	Cmd_AddCommand( "ads_mover", CL_AdsGirar_f, "custom: <pitch> <yaw> <roll> added to the turn of the weapon in hand while aiming (seen as the weapon moving)" );
	Cmd_AddCommand( "ads_editar", CL_AdsEditar_f, "custom: the mouse places the weapon in hand while aiming instead of turning the view, again to leave it and save ads.txt" );
	Cmd_AddCommand( "ads_fijo", CL_AdsFijo_f, "custom: keeps the aim up without the button (to adjust it), again to let it go" );
	Cmd_AddCommand( "ads_recargar", CL_AdsCargar, "custom: reads ads.txt again" );
}
