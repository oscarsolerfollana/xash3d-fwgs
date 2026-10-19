/*
cl_variantes.c - weapon variants, client side (see common/variantes.h)
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
#include "variantes.h"

/*
The server swaps the player model of whoever holds a variant. client.dll
predicts the local weapon itself and sets the base view model and plays the
base fire sounds from its events, so both are swapped here by looking at the
player model the server sent for that player.
*/

static variante_t	cl_variantes[MAX_VARIANTES];
static int		cl_numvariantes;
static int		cl_variantes_servercount = -1;

static void CL_VariantesCargar( void )
{
	if( cl_variantes_servercount == cl.servercount )
		return;
	cl_variantes_servercount = cl.servercount;
	cl_numvariantes = Variantes_Cargar( cl_variantes, MAX_VARIANTES );
}

static const char *CL_NombreModelo( int index )
{
	model_t *m = CL_ModelHandle( index );
	return m ? m->name : "";
}

static int CL_ModeloArma( int ent )
{
	if( ent == cl.playernum + 1 )
		return cl.frames[cl.parsecountmod].playerstate[cl.playernum].weaponmodel;
	if( ent > 0 && ent <= cl.maxclients )
	{
		cl_entity_t *e = CL_GetEntityByIndex( ent );
		if( e )
			return e->curstate.weaponmodel;
	}
	return 0;
}

static const variante_t *CL_VarianteDe( int ent )
{
	const char *p;
	int i, modelo;

	CL_VariantesCargar();
	if( !cl_numvariantes )
		return NULL;

	modelo = CL_ModeloArma( ent );
	if( !modelo )
		return NULL;

	p = CL_NombreModelo( modelo );
	for( i = 0; i < cl_numvariantes; i++ )
	{
		if( !Q_stricmp( p, cl_variantes[i].p_nuevo ))
			return &cl_variantes[i];
	}
	return NULL;
}

/*
==============
CL_VariantesViewModel

the base view model of the local player becomes the variant's
==============
*/
int CL_VariantesViewModel( int index )
{
	const variante_t *v;
	int i;

	if( !index )
		return index;

	v = CL_VarianteDe( cl.playernum + 1 );
	if( !v || !Variantes_EnLista( v->v_base, CL_NombreModelo( index )))
		return index;

	for( i = 1; i <= cl.nummodels; i++ )
	{
		if( cl.models[i] && !Q_stricmp( cl.models[i]->name, v->v_nuevo ))
			return i;
	}
	return index;
}

/*
==============
CL_VariantesPrediccion

after client.dll has predicted a command: the rate of fire and the reload
time of a variant, as the server does them (sv_variantes.c), or what is seen
and heard would not match what is fired
==============
*/
void CL_VariantesPrediccion( const local_state_t *from, local_state_t *to )
{
	const variante_t *v;
	const weapon_data_t *antes;
	weapon_data_t *ahora;
	int id = to->client.m_iId;

	if( id <= 0 || id >= MAX_LOCAL_WEAPONS )
		return;

	v = CL_VarianteDe( cl.playernum + 1 );
	if( !v || ( v->cadencia == 1.0f && v->recarga == 1.0f ))
		return;

	antes = &from->weapondata[id];
	ahora = &to->weapondata[id];

	if( v->cadencia != 1.0f && ahora->m_iClip < antes->m_iClip && ahora->m_flNextPrimaryAttack > 0.0f )
		ahora->m_flNextPrimaryAttack /= v->cadencia;

	if( v->recarga != 1.0f && ahora->m_fInReload && !antes->m_fInReload && to->client.m_flNextAttack > 0.0f )
		to->client.m_flNextAttack *= v->recarga;
}

/*
==============
CL_VariantesSonido

a base fire sound played for a player holding a variant becomes the variant's
==============
*/
const char *CL_VariantesSonido( int ent, const char *sample )
{
	const variante_t *v;
	int i;

	if( !sample || !*sample )
		return sample;

	v = CL_VarianteDe( ent );
	if( !v || !v->sonido[0] )
		return sample;

	for( i = 0; i < v->num_sonidos; i++ )
	{
		if( !Q_strnicmp( sample, v->sonidos_base[i], Q_strlen( v->sonidos_base[i] )))
			return v->sonido;
	}
	return sample;
}

/*
==============
CL_VariantesPrecision

how far off the view the bullets of what the player holds go, times the base
weapon's (as the server has it, sv_variantes.c)
==============
*/
float CL_VariantesPrecision( int ent )
{
	const variante_t *v = CL_VarianteDe( ent );

	return v ? v->precision : 1.0f;
}

/*
==============
CL_VariantesRetroceso

how far the shots of what the player holds kick the view, times the base
weapon's, and to the sides times that again
==============
*/
float CL_VariantesRetroceso( int ent, float *lateral )
{
	const variante_t *v = CL_VarianteDe( ent );

	*lateral = v ? v->lateral : 1.0f;
	return v ? v->retroceso : 1.0f;
}
