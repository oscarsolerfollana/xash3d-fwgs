/*
cl_marcador.c - Firearms: the artillery smoke marker is held and thrown
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
"useskill marker" (the M key) doesn't go to the server straight away: the
smoke grenade comes to the hand instead (Counter-Strike's, if
patches/marcador_cs.py copied it, or else Firearms' concussion grenade, a can
too), and is held as Firearms' grenades are: pressing
the primary attack pulls the pin, and the can stays in the hand for as long as
it is held; letting it go throws it. As it leaves the hand the command is sent
(the server throws the marker, sv_marcador.c). The secondary attack, M again
(before the pin is out), another weapon or dying put it away. While it is in the hand the weapon
neither fires nor reloads. Only what is seen: the game still has the weapon in
the hand.
*/

static CVAR_DEFINE_AUTO( cl_marcador_mano, "1", FCVAR_ARCHIVE, "custom: Firearms: the artillery marker (useskill marker) is taken in the hand and thrown with the primary attack" );
static CVAR_DEFINE_AUTO( cl_marcador_espejo, "1", FCVAR_ARCHIVE, "custom: Firearms: Counter-Strike's smoke grenade, a left-handed model, is drawn mirrored, in the right hand" );

#define MARCADOR_SUELTA		0.15	// seconds into the throw it leaves the hand

// the view models it can be, the first one the server precached: Counter-Strike's smoke
// grenade (sv_marcador.c), or else Firearms' concussion grenade
static const struct
{
	const char	*modelo;
	const char	*sacar, *listo, *anilla, *lanzar;
	float		frame;	// of the pin animation, held while the button is: the pin out and the can in sight
	qboolean		zurdo;	// in the left hand, as Firearms' are in the right
} marcador_modelos[] =
{
{ "models/v_humo_cs.mdl", "deploy", "idle", "pullpin", "throw", 40.0f, true },
{ "models/v_conc.mdl", "deploy", "idle_pin", "pin_pull", "throw", 35.0f, false },
};

#define MODELO	marcador_modelos[modelo_num]

enum
{
	M_NADA = 0,
	M_SACANDO,	// deploy
	M_LISTO,	// idle_pin: in the hand, the pin in
	M_ANILLA,	// pin_pull, held on MARCADOR_ANILLA until the button is let go
	M_LANZANDO,	// throw
};

static int	estado;
static double	inicio;		// of the animation playing
static int	secuencia;
static int	arma;		// the weapon's view model when it was taken
static qboolean	pendiente;	// pressed while it was being drawn
static qboolean	enviado;
static qboolean	ataque_antes;
static qboolean	ataque;		// the primary attack held
static int	modelo_num;	// in marcador_modelos

static int CL_MarcadorModelo( void )
{
	int i, j;

	for( j = 0; j < ARRAYSIZE( marcador_modelos ); j++ )
	{
		for( i = 1; i <= cl.nummodels; i++ )
		{
			if( cl.models[i] && !Q_stricmp( cl.models[i]->name, marcador_modelos[j].modelo ))
			{
				modelo_num = j;
				return i;
			}
		}
	}

	return 0;
}

static mstudioseqdesc_t *CL_MarcadorSecuencia( model_t *mod, const char *label, int *num )
{
	studiohdr_t	*hdr;
	mstudioseqdesc_t	*seq;
	int		i;

	if( !mod || mod->type != mod_studio )
		return NULL;

	hdr = (studiohdr_t *)Mod_StudioExtradata( mod );
	if( !hdr )
		return NULL;

	seq = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex );
	for( i = 0; i < hdr->numseq; i++ )
	{
		if( !Q_stricmp( seq[i].label, label ) || ( label[0] == '*' && Q_stristr( seq[i].label, label + 1 )))
		{
			if( num ) *num = i;
			return &seq[i];
		}
	}

	return NULL;
}

static void CL_MarcadorPoner( int nuevo, const char *label )
{
	int num = 0;

	if( !CL_MarcadorSecuencia( CL_ModelHandle( CL_MarcadorModelo( )), label, &num ))
		num = 0;

	estado = nuevo;
	secuencia = num;
	inicio = cl.time;
	Con_Reportf( "marcador: %s\n", label );
}

static float CL_MarcadorDuracion( void )
{
	model_t		*mod = CL_ModelHandle( CL_MarcadorModelo( ));
	studiohdr_t	*hdr = mod ? (studiohdr_t *)Mod_StudioExtradata( mod ) : NULL;
	mstudioseqdesc_t	*seq;

	if( !hdr || secuencia >= hdr->numseq )
		return 0.0f;

	seq = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex ) + secuencia;
	return seq->fps > 0.0f ? ( seq->numframes - 1 ) / seq->fps : 0.0f;
}

static float CL_MarcadorFotograma( void )
{
	model_t		*mod = CL_ModelHandle( CL_MarcadorModelo( ));
	studiohdr_t	*hdr = mod ? (studiohdr_t *)Mod_StudioExtradata( mod ) : NULL;
	mstudioseqdesc_t	*seq;

	if( !hdr || secuencia >= hdr->numseq )
		return 0.0f;

	seq = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex ) + secuencia;
	return ( cl.time - inicio ) * seq->fps;
}

/*
==============
CL_MarcadorGuardar

back to the weapon, drawing it if it has an animation for it
==============
*/
static void CL_MarcadorGuardar( void )
{
	int num;

	estado = M_NADA;
	pendiente = false;

	if( cl.local.viewmodel == arma && ( CL_MarcadorSecuencia( CL_ModelHandle( CL_VariantesViewModel( arma )), "*deploy", &num )
		|| CL_MarcadorSecuencia( CL_ModelHandle( CL_VariantesViewModel( arma )), "*draw", &num )))
	{
		cl.local.weaponsequence = num;
		cl.local.weaponstarttime = cl.time;
	}
}

static void CL_MarcadorEnviar( void )
{
	if( cls.state < ca_connected || cls.demoplayback )
		return;

	MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
	MSG_WriteString( &cls.netchan.message, "useskill marker" );
}

/*
==============
CL_Useskill_f

the game's command goes on to the server, but for the marker
==============
*/
static void CL_Useskill_f( void )
{
	qboolean marcador = Cmd_Argc() == 2 && !Q_stricmp( Cmd_Argv( 1 ), "marker" );

	if( !marcador || !cl_marcador_mano.value || Q_strnicmp( GI->gamefolder, "firearms", 8 )
		|| cls.state != ca_active || cls.demoplayback || cl.local.health <= 0 || !CL_MarcadorModelo( ))
	{
		Cmd_ForwardToServer();
		return;
	}

	if( estado == M_LANZANDO )
		return;

	if( estado != M_NADA )
	{
		if( estado == M_SACANDO || estado == M_LISTO )
			CL_MarcadorGuardar();
		return;
	}

	arma = cl.local.viewmodel;
	enviado = false;
	pendiente = false;
	CL_MarcadorPoner( M_SACANDO, MODELO.sacar );
}

qboolean CL_MarcadorActivo( void )
{
	return estado != M_NADA;
}

/*
==============
CL_MarcadorEspejo

for the renderer (PARM_VIEWMODEL_FLIP): the grenade in the hand is a left-handed one
==============
*/
qboolean CL_MarcadorEspejo( void )
{
	return estado != M_NADA && cl_marcador_espejo.value && MODELO.zurdo && clgame.viewent.model
		&& !Q_stricmp( clgame.viewent.model->name, MODELO.modelo );
}

/*
==============
CL_MarcadorCmd

after client.dll made the command: the attack throws, the rest is held back
==============
*/
void CL_MarcadorCmd( usercmd_t *cmd, qboolean active )
{
	ataque = active && FBitSet( cmd->buttons, IN_ATTACK );

	if( estado != M_NADA && active )
	{
		if( ataque && !ataque_antes )
			pendiente = true;

		if( FBitSet( cmd->buttons, IN_ATTACK2 ) && ( estado == M_SACANDO || estado == M_LISTO ))
			CL_MarcadorGuardar();
	}

	ataque_antes = ataque;

	if( estado != M_NADA )
		ClearBits( cmd->buttons, IN_ATTACK|IN_ATTACK2|IN_RELOAD );
}

/*
==============
CL_MarcadorFrame

every frame, after the weapon model was set up: the grenade instead
==============
*/
void CL_MarcadorFrame( void )
{
	cl_entity_t	*view = &clgame.viewent;
	int		modelo;

	if( estado == M_NADA )
		return;

	modelo = CL_MarcadorModelo();

	// another weapon taken, dead, or gone
	if( !modelo || cl.local.viewmodel != arma || cl.local.health <= 0 || cls.state != ca_active )
	{
		if( estado == M_LANZANDO && !enviado )
			CL_MarcadorEnviar();
		estado = M_NADA;
		pendiente = false;
		return;
	}

	switch( estado )
	{
	case M_SACANDO:
		if( cl.time - inicio >= CL_MarcadorDuracion( ))
			CL_MarcadorPoner( M_LISTO, MODELO.listo );
		break;
	case M_LISTO:
		if( pendiente )
		{
			pendiente = false;
			CL_MarcadorPoner( M_ANILLA, MODELO.anilla );
		}
		else if( cl.time - inicio >= CL_MarcadorDuracion( ))
			inicio = cl.time; // the idle again
		break;
	case M_ANILLA:
		// the pin out, it stays there while the button is held (later in
		// Firearms' pin_pull, and in its "prepare", the can goes out of sight)
		if( CL_MarcadorFotograma( ) >= MODELO.frame && !ataque )
			CL_MarcadorPoner( M_LANZANDO, MODELO.lanzar );
		break;
	case M_LANZANDO:
		if( !enviado && cl.time - inicio >= MARCADOR_SUELTA )
		{
			CL_MarcadorEnviar();
			enviado = true;
		}
		if( cl.time - inicio >= CL_MarcadorDuracion( ))
		{
			CL_MarcadorGuardar();
			return;
		}
		break;
	}

	view->curstate.modelindex = modelo;
	view->model = CL_ModelHandle( modelo );
	view->curstate.sequence = secuencia;
	view->curstate.animtime = inicio;
	view->curstate.frame = 0.0f;
	view->curstate.framerate = 1.0f;

	if( estado == M_ANILLA && CL_MarcadorFotograma( ) >= MODELO.frame )
	{
		view->curstate.frame = MODELO.frame;
		view->curstate.framerate = 0.0f;
	}
}

void CL_InitMarcador( void )
{
	Cvar_RegisterVariable( &cl_marcador_mano );
	Cvar_RegisterVariable( &cl_marcador_espejo );
	Cmd_AddCommand( "useskill", CL_Useskill_f, "custom: Firearms skills; useskill marker takes the smoke grenade in the hand (cl_marcador_mano)" );
}
