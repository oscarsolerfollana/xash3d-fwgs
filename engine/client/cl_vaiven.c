/*
cl_vaiven.c - the sway of the aim, with the realistic precision
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
precision realista (the precision_realista cvar): aiming down the sights the
bullets go where the sights are (ads_bala.c), and what moves is the aim. It
sways up and down like breathing, each swing of its own size and speed
within a range, and from side to side slower and less. How much comes from:

  - the posture: standing, crouched, prone, and the bipod down almost nothing
  - moving, by the speed
  - the weight of the weapon (precision.txt): more, and slower
  - being tired (the Stamina of Firearms, 0 to 64): more, and the range of
    the size and the speed of the swings gets wider
  - wounds: the health left, and a bullet in an arm (vaiven_brazo, the server
    tells it) until dying
  - the marksmanship skill (loadpreconfig 23 1 and 23 2, sv_useskills)
  - each shot and each landing, for a moment

The sway turns the view itself, so the server shoots where it goes, and the
mouse can follow it back. precision clasica / realista changes the mode.
*/

static CVAR_DEFINE_AUTO( cl_vaiven, "1", FCVAR_ARCHIVE, "custom: general multiplier of the sway of the aim with the realistic precision (precision realista): 2 sways twice as much, 0.5 half" );
static CVAR_DEFINE_AUTO( cl_vaiven_debug, "0", 0, "custom: what the sway of the aim comes from, on the top right" );

#define VAIVEN_BASE		0.75f	// degrees up and down, standing still, rested, a rifle of 4 kg
#define VAIVEN_LATERAL		0.55f	// from side to side, times that
#define STAMINA_MAX		64.0f

static float	stamina = STAMINA_MAX;	// the last Stamina of the game
static qboolean	brazo;			// a bullet in an arm since spawning
static int	punteria_tokens;	// marksmanship chosen in the class (0 to 2)
static float	impulso;		// shots and landings, dying away

static struct
{
	float	fase[2];	// up and down, side to side
	float	freq[2];	// swings per second now
	float	amp[2];		// size now
	float	azar[2];	// of this swing, times what it has to be
	vec2_t	antes;		// the offset given last frame
	qboolean	activo;
} v;

static float	vz_antes;		// to see a landing
static int	suelo_antes = 1;

/*
==============
CL_PrecisionRealista

the mode is precision realista (precision_realista, sv_main.c)
==============
*/
qboolean CL_PrecisionRealista( void )
{
	return Cvar_VariableValue( "precision_realista" ) != 0.0f;
}

/*
==============
CL_VaivenMensaje

the user messages of the game it needs (cl_parse.c)
==============
*/
void CL_VaivenMensaje( const char *nombre, const byte *buf, int size )
{
	if( size >= 1 && !Q_strcmp( nombre, "Stamina" ))
		stamina = buf[0];
}

/*
==============
CL_VaivenComando

what the client sends to the server: the skills of the class
==============
*/
void CL_VaivenComando( const char *s )
{
	int menu, opcion;

	while( *s == ' ' )
		s++;

	if( !Q_strnicmp( s, "usingpreconfig", 14 ))
		punteria_tokens = 0;
	else if( sscanf( s, "loadpreconfig %d %d", &menu, &opcion ) == 2 && menu == 23 && ( opcion == 1 || opcion == 2 ))
		punteria_tokens = Q_min( punteria_tokens + 1, 2 );
}

static int CL_VaivenPunteria( void )
{
	int skills = (int)Cvar_VariableValue( "sv_useskills" );

	if( skills <= 0 )
		return 0;
	if( skills >= 3 )
		return 2; // all the skills
	return punteria_tokens;
}

static void CL_VaivenBrazo_f( void )
{
	brazo = true;
}

/*
==============
CL_VaivenDisparo

a shot of the local player (cl_ads.c, with the kick of the view)
==============
*/
void CL_VaivenDisparo( void )
{
	impulso = Q_min( impulso + 0.6f, 2.5f );
}

/*
==============
CL_VaivenPostura

0 standing, 1 crouched, 2 prone. Firearms leaves the eye where it is
crouched (the hull is the small one) and puts it under the origin prone
==============
*/
static int CL_VaivenPostura( void )
{
	if( cl.viewheight[2] <= 0.0f )
		return 2;
	if( cl.local.usehull == 1 || cl.viewheight[2] < 20.0f )
		return 1;
	return 0;
}

/*
==============
CL_VaivenAmplitud

how far the aim sways now, up and down, in degrees, and how tired (0 to 1)
==============
*/
static float CL_VaivenAmplitud( float kg, qboolean bipode, float *cansancio, float *f_peso )
{
	static const float	posturas[3] = { 1.0f, 0.65f, 0.35f };
	float	velocidad = sqrt( cl.simvel[0] * cl.simvel[0] + cl.simvel[1] * cl.simvel[1] );
	float	f_postura = bipode ? 0.06f : posturas[CL_VaivenPostura()];
	float	f_mov = 1.0f + 2.5f * Q_min( velocidad / 200.0f, 1.5f );
	float	f_cansancio, f_heridas, f_habilidad;
	int	punteria = CL_VaivenPunteria();

	*cansancio = bound( 0.0f, 1.0f - stamina / STAMINA_MAX, 1.0f );
	*f_peso = 0.7f + 0.08f * kg;
	f_cansancio = 1.0f + 2.0f * *cansancio;
	f_heridas = ( 1.0f + 1.2f * bound( 0.0f, 1.0f - cl.local.health / 100.0f, 1.0f )) * ( brazo ? 1.6f : 1.0f );
	f_habilidad = punteria == 2 ? 0.6f : punteria == 1 ? 0.8f : 1.0f;

	if( cl_vaiven_debug.value )
	{
		con_nprint_t info = { 0 };

		info.time_to_live = 0.2f;
		info.color[0] = info.color[1] = info.color[2] = 1.0f;
		info.index = 24;
		Con_NXPrintf( &info, "vaiven: postura %d (%.0f) x%.2f  mov x%.2f  peso %.1f kg x%.2f", CL_VaivenPostura(), cl.viewheight[2], f_postura, f_mov, kg, *f_peso );
		info.index = 25;
		Con_NXPrintf( &info, "cansancio %.2f (stamina %.0f) x%.2f  heridas x%.2f%s  punteria %d x%.2f  impulso %.2f", *cansancio, stamina, f_cansancio, f_heridas, brazo ? " (brazo)" : "", punteria, f_habilidad, impulso );
	}

	return VAIVEN_BASE * cl_vaiven.value * f_postura * f_mov * *f_peso * f_cansancio * f_heridas * f_habilidad * ( 1.0f + impulso );
}

/*
==============
CL_VaivenNuevo

the next swing of an axis: its size and speed, at random within a range that
gets wider when tired. Heavy weapons swing slower
==============
*/
static void CL_VaivenNuevo( int eje, float cansancio, float f_peso )
{
	float	lento = bound( 0.7f, 1.3f - 0.3f * f_peso, 1.1f );
	float	freq;

	if( eje == 0 )
		freq = COM_RandomFloat( 0.2f, 0.35f + 0.5f * cansancio );
	else freq = v.freq[0] * COM_RandomFloat( 0.4f, 0.7f );

	v.freq[eje] = freq * ( eje == 0 ? lento : 1.0f );
	v.azar[eje] = COM_RandomFloat( 0.6f - 0.3f * cansancio, 1.0f );
}

/*
==============
CL_VaivenAplicar

every command while aiming (cl_ads.c): frac is how far the aim came up, the
sway is added to the view by what it changed since the last one
==============
*/
void CL_VaivenAplicar( usercmd_t *cmd, float frac, const char *modelo, qboolean bipode )
{
	float	dt = host.frametime;
	float	cansancio, f_peso, amplitud, kg, meta;
	vec2_t	off;
	int	i;

	// landing from a fall
	if( cl.local.onground != -1 && suelo_antes == -1 && vz_antes < -150.0f )
		impulso = Q_min( impulso + Q_min(( -vz_antes - 150.0f ) / 300.0f, 1.5f ), 2.5f );
	suelo_antes = cl.local.onground;
	vz_antes = cl.simvel[2];

	impulso *= exp( -dt / 0.4f );

	// a new life: the arm is fine again
	if( cl.local.health <= 0 )
		brazo = false;

	if( !CL_PrecisionRealista() || frac <= 0.0f || dt <= 0.0f || cl.paused )
	{
		v.antes[0] = v.antes[1] = 0.0f;
		v.activo = false;
		return;
	}

	kg = COM_ArmaReal( modelo, NULL );
	amplitud = CL_VaivenAmplitud( kg, bipode, &cansancio, &f_peso );

	if( !v.activo )
	{
		v.activo = true;
		v.fase[0] = v.fase[1] = 0.0f;
		for( i = 0; i < 2; i++ )
		{
			CL_VaivenNuevo( i, cansancio, f_peso );
			v.amp[i] = amplitud * ( i == 0 ? 1.0f : VAIVEN_LATERAL ) * v.azar[i];
		}
	}

	for( i = 0; i < 2; i++ )
	{
		v.fase[i] += M_PI2_F * v.freq[i] * dt;

		// a swing finished: the next one from here, without a jump
		if( v.fase[i] >= M_PI2_F )
		{
			v.fase[i] -= M_PI2_F;
			CL_VaivenNuevo( i, cansancio, f_peso );
		}

		// the size follows what it has to be smoothly, quicker growing
		meta = amplitud * ( i == 0 ? 1.0f : VAIVEN_LATERAL ) * v.azar[i];
		v.amp[i] += ( meta - v.amp[i] ) * ( 1.0f - exp( -dt / ( meta > v.amp[i] ? 0.15f : 0.6f )));
	}

	// up and down with a bit of its double, side to side: a loose eight
	off[0] = v.amp[0] * ( sin( v.fase[0] ) + 0.2f * sin( 2.0f * v.fase[0] + 1.0f ));
	off[1] = v.amp[1] * sin( v.fase[1] );
	off[0] *= frac;
	off[1] *= frac;

	if( cl_vaiven_debug.value >= 2.0f )
	{
		static double ultimo;

		if( host.realtime - ultimo >= 0.1 )
		{
			ultimo = host.realtime;
			Con_Printf( "[vaiven] %.2f off %.3f %.3f amp %.3f %.3f freq %.2f %.2f ojo %.0f hull %d suelo %d vz %.0f cansancio %.2f impulso %.2f\n", cl.time, off[0], off[1], v.amp[0], v.amp[1], v.freq[0], v.freq[1], cl.viewheight[2], cl.local.usehull, cl.local.onground, cl.simvel[2], cansancio, impulso );
		}
	}

	cl.viewangles[PITCH] += off[0] - v.antes[0];
	cl.viewangles[YAW] += off[1] - v.antes[1];
	Vector2Copy( off, v.antes );

	VectorCopy( cl.viewangles, cmd->viewangles );
}

static void CL_Precision_f( void )
{
	const char *modo = Cmd_Argv( 1 );

	if( !Q_stricmp( modo, "realista" ))
		Cvar_Set( "precision_realista", "1" );
	else if( !Q_stricmp( modo, "clasica" ))
		Cvar_Set( "precision_realista", "0" );
	else
	{
		Con_Printf( "precision clasica | realista (now %s)\n", CL_PrecisionRealista() ? "realista" : "clasica" );
		return;
	}

	COM_ArmasRealesRecargar();
	Con_Printf( "precision %s\n", CL_PrecisionRealista() ? "realista: aiming, the bullets go where the sights are and the aim sways" : "clasica: the spread of the game" );
}

void CL_InitVaiven( void )
{
	Cvar_RegisterVariable( &cl_vaiven );
	Cvar_RegisterVariable( &cl_vaiven_debug );
	Cmd_AddCommand( "precision", CL_Precision_f, "custom: precision clasica (the spread of the game) or realista (aiming, the bullets go where the sights are, and the aim sways)" );
	Cmd_AddCommand( "vaiven_brazo", CL_VaivenBrazo_f, "custom: from the server, a bullet in an arm: the aim sways more until dying" );
}
