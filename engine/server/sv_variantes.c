/*
sv_variantes.c - weapon variants, server side (see common/variantes.h)
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
#include "variantes.h"

/*
The game dll never hears of a variant: the class menu sends
"loadpreconfig <menu> <variant option>", and before the game sees it the
option is turned into the base weapon's one and the variant is noted for that
player. From then on, whenever the player holds the base weapon its player
and view models are swapped for the variant's (the client swaps the view
model and the fire sound by looking at the player model), and the world model
it leaves when dropped too. Choosing the base weapon again clears it.
*/

/*
A variant can also have bigger (or smaller) magazines than its base. The game
keeps, for every player and ammo type, a circular list of magazines with the
rounds left in each; it makes them full (the base size) when it hands out
ammo, and a reload stores what is left in the loaded one (the head of the
list) and loads the next. So every magazine of that ammo type that turns up
full gets the new size, once, and so does the clip of the weapon when its
magazine is first seen. The rate of fire: when the weapon has fired since
the last frame, the time left to the next shot is divided; the reload: when
one starts, the player's time to the end of it is multiplied (the client
does the same in its prediction, cl_variantes.c). Spare magazines: when the
list is new (the player has just spawned) some are taken out of it.
Thrown things (a grenade whose world model is swapped) go faster or slower:
their speed is scaled in the first frame after the game has set it. These
are the offsets in the game's C++ objects (pvPrivateData) of Firearms 3.0,
which has no source.
*/
#define FA_ARMA_JUGADOR		0x4c	// CBasePlayerItem::m_pPlayer
#define FA_ARMA_MUNICION	0x8c	// m_iPrimaryAmmoType
#define FA_ARMA_CARGADOR	0x94	// m_iClip
#define FA_ARMA_MAXCARGADOR	0xb0	// magazine size a reload tops up to
#define FA_ARMA_SIGUIENTE	0x80	// m_flNextPrimaryAttack (relative: the weapon time base is 0)
#define FA_ARMA_RECARGANDO	0xa8	// m_fInReload
#define FA_JUGADOR_SIGUIENTE	0x104	// CBasePlayer::m_flNextAttack (end of the reload)
#define FA_JUGADOR_MUNICION	0xadc	// per ammo type: number of magazines
#define FA_JUGADOR_CARGADORES	0xbdc	// per ammo type: list of { next, rounds }
#define FA_MAX_MUNICION		32

/*
The damage of the bullets is an argument of the game's
CBaseEntity::FireBulletsPlayer (at 0x10031b90 in Firearms 3.0's dll, iDamage,
the 14th): it is read from the stack for each hit, after the trace of the
bullet, which goes through the engine. That trace is asked for from the code
after FA_DISPARO_RETORNO, and the argument is FA_DISPARO_DANO bytes up the
stack from where that return address is.
*/
#define FA_DISPARO_RETORNO	0x31f27	// in the game dll, after the call to UTIL_TraceLine
#define FA_DISPARO_DANO		0x164
#define FA_GIVEFNPTRS		0x4bda0	// where GiveFnptrsToDll is in it, to know it is the one
static const byte fa_givefnptrs[] = { 0x8b, 0x44, 0x24, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x57, 0xb9, 0x90 };	// its first bytes
static const byte fa_disparo_codigo[] = { 0xd9, 0x44, 0x24, 0x78 };	// fld [esp+0x78], after the call
#define MAX_VISTOS		64

static variante_t	sv_variantes[MAX_VARIANTES];
static int		sv_numvariantes;
static string_t		sv_vstr[MAX_VARIANTES], sv_pstr[MAX_VARIANTES];
static qboolean		sv_elegida[MAX_CLIENTS][MAX_VARIANTES];
static void		*sv_vistos[MAX_CLIENTS][MAX_VISTOS];	// magazines already resized
static int		sv_numvistos[MAX_CLIENTS];
static int		sv_balas[MAX_CLIENTS];		// rounds in the weapon last frame
static int		sv_lanzados[16];		// thrown entities whose speed is still to change
static float		sv_lanzados_f[16];
static int		sv_numlanzados;
static int		sv_recargando[MAX_CLIENTS];	// m_fInReload last frame

static CVAR_DEFINE_AUTO( variantes_debug, "0", 0, "custom: print the class menu commands and the weapon variant swaps" );

void SV_VariantesInit( void )
{
	Cvar_RegisterVariable( &variantes_debug );
}

/*
==============
SV_VariantesActivar

after the game precached its resources, while the server is still loading
==============
*/
void SV_VariantesActivar( void )
{
	int i;

	memset( sv_elegida, 0, sizeof( sv_elegida ));
	memset( sv_numvistos, 0, sizeof( sv_numvistos ));
	sv_numlanzados = 0;
	sv_numvariantes = Variantes_Cargar( sv_variantes, MAX_VARIANTES );

	for( i = 0; i < sv_numvariantes; i++ )
	{
		variante_t *v = &sv_variantes[i];

		SV_ModelIndex( v->v_nuevo );
		SV_ModelIndex( v->p_nuevo );
		if( Q_strcmp( v->w_nuevo, "-" ))
			SV_ModelIndex( v->w_nuevo );
		if( v->sonido[0] )
			SV_SoundIndex( v->sonido );
		sv_vstr[i] = SV_AllocString( v->v_nuevo );
		sv_pstr[i] = SV_AllocString( v->p_nuevo );
	}

	if( sv_numvariantes )
		Con_Reportf( "%d weapon variants\n", sv_numvariantes );
}

/*
==============
SV_VariantesComando

"loadpreconfig <menu> <option>": a variant option becomes the base one
==============
*/
void SV_VariantesComando( sv_client_t *cl )
{
	int	slot = cl - svs.clients;
	int	menu, opcion, i;

	if( variantes_debug.value && Q_stricmp( Cmd_Argv( 0 ), "fullupdate" ))
		Con_Printf( "[variantes] %s: %s %s\n", cl->name, Cmd_Argv( 0 ), Cmd_Args( ) ? Cmd_Args( ) : "" );

	if( !sv_numvariantes || Cmd_Argc() != 3 || Q_stricmp( Cmd_Argv( 0 ), "loadpreconfig" ))
		return;

	menu = Q_atoi( Cmd_Argv( 1 ));
	opcion = Q_atoi( Cmd_Argv( 2 ));

	for( i = 0; i < sv_numvariantes; i++ )
	{
		variante_t *v = &sv_variantes[i];
		int j;

		if( v->menu != menu || ( opcion != v->opcion && opcion != v->base ))
			continue;

		// one variant per base weapon
		for( j = 0; j < sv_numvariantes; j++ )
		{
			if( sv_variantes[j].menu == v->menu && sv_variantes[j].base == v->base )
				sv_elegida[slot][j] = false;
		}

		if( opcion == v->opcion )
		{
			sv_elegida[slot][i] = true;
			Cmd_TokenizeString( va( "loadpreconfig %d %d", menu, v->base ));
			if( variantes_debug.value )
				Con_Printf( "[variantes] %s -> %s (loadpreconfig %d %d)\n", cl->name, v->token, menu, v->base );
			return;
		}
	}
}

/*
==============
SV_VariantesCargadores

magazines of the base weapon of a player with the variant
==============
*/
static void SV_VariantesCargadores( int slot, edict_t *jugador, const variante_t *v )
{
	byte	*pjugador = jugador->pvPrivateData;
	void	*vistos[MAX_VISTOS];
	int	numvistos = 0;
	int	i;

	if( !pjugador )
		return;

	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t	*e = SV_EdictNum( i );
		byte	*arma;
		int	*cabeza, *n;
		int	tipo, j, k;

		if( !SV_IsValidEdict( e ) || e->v.owner != jugador || !e->pvPrivateData )
			continue;
		if( Q_strcmp( SV_GetString( e->v.classname ), v->cargador_arma ))
			continue;

		arma = e->pvPrivateData;
		if( *(byte **)( arma + FA_ARMA_JUGADOR ) != pjugador )
			continue; // not the layout we know

		tipo = *(int *)( arma + FA_ARMA_MUNICION );
		if( tipo < 0 || tipo >= FA_MAX_MUNICION )
			continue;

		if( v->cargador_base > 0 )
			*(int *)( arma + FA_ARMA_MAXCARGADOR ) = v->cargador_nuevo;

		{
			int	balas = *(int *)( arma + FA_ARMA_CARGADOR );
			int	recargando = *(int *)( arma + FA_ARMA_RECARGANDO );
			float	*siguiente = (float *)( arma + FA_ARMA_SIGUIENTE );
			float	*fin = (float *)( pjugador + FA_JUGADOR_SIGUIENTE );

			// it has fired: the next shot comes sooner
			if( v->cadencia != 1.0f && balas < sv_balas[slot] && *siguiente > 0.0f )
				*siguiente /= v->cadencia;
			// a reload has started: it ends later
			if( v->recarga != 1.0f && recargando && !sv_recargando[slot] && *fin > 0.0f )
				*fin *= v->recarga;

			if( variantes_debug.value >= 2 && balas != sv_balas[slot] )
				Con_Printf( "[variantes] %s: %d balas en el arma (siguiente %.3f)\n", svs.clients[slot].name, balas, *siguiente );
			if( variantes_debug.value >= 2 && recargando && !sv_recargando[slot] )
				Con_Printf( "[variantes] %s: recarga de %.2f s\n", svs.clients[slot].name, *fin );
			sv_balas[slot] = balas;
			sv_recargando[slot] = recargando;
		}

		cabeza = *(int **)( pjugador + FA_JUGADOR_CARGADORES + tipo * 4 );

		// a new list (just spawned): fewer spare magazines
		if( cabeza && sv_numvistos[slot] == 0 && v->repuesto < 0 )
		{
			int *cuenta = (int *)( pjugador + FA_JUGADOR_MUNICION + tipo * 4 );

			for( k = 0; k < -v->repuesto; k++ )
			{
				int *quitar = (int *)cabeza[0];

				if( !quitar || quitar == cabeza || *cuenta <= 1 )
					break;
				cabeza[0] = quitar[0];	// out of the list (its 8 bytes stay allocated)
				( *cuenta )--;
				if( variantes_debug.value )
					Con_Printf( "[variantes] %s: un cargador menos (%d)\n", svs.clients[slot].name, *cuenta );
			}
		}
		for( n = cabeza, k = 0; n && k < MAX_VISTOS && numvistos < MAX_VISTOS; k++ )
		{
			for( j = 0; j < sv_numvistos[slot]; j++ )
			{
				if( sv_vistos[slot][j] == n )
					break;
			}

			if( j == sv_numvistos[slot] )
			{
				if( v->cargador_base > 0 && n[1] == v->cargador_base )
				{
					n[1] = v->cargador_nuevo;

					// the loaded magazine: the weapon keeps its own count
					if( n == cabeza && *(int *)( arma + FA_ARMA_CARGADOR ) == v->cargador_base )
						*(int *)( arma + FA_ARMA_CARGADOR ) = v->cargador_nuevo;

					if( variantes_debug.value )
						Con_Printf( "[variantes] %s: cargador de %d -> %d\n", svs.clients[slot].name, v->cargador_base, v->cargador_nuevo );
				}
			}

			vistos[numvistos++] = n;
			n = (int *)n[0];
			if( n == cabeza )
				break;
		}
	}

	// only what is in the lists now: a freed magazine may come back at the same address
	memcpy( sv_vistos[slot], vistos, numvistos * sizeof( vistos[0] ));
	sv_numvistos[slot] = numvistos;
}

/*
==============
SV_VariantesFrame

after the game frame, before the clients are sent their data
==============
*/
void SV_VariantesFrame( void )
{
	int i, j;

	if( !sv_numvariantes )
		return;

	for( i = 0; i < sv_numlanzados; i++ )
	{
		edict_t *e = SV_EdictNum( sv_lanzados[i] );

		if( SV_IsValidEdict( e ) && !VectorIsNull( e->v.velocity ))
		{
			VectorScale( e->v.velocity, sv_lanzados_f[i], e->v.velocity );
			if( variantes_debug.value )
				Con_Printf( "[variantes] lanzado a %.0f\n", VectorLength( e->v.velocity ));
		}
	}
	sv_numlanzados = 0;

	for( i = 0; i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		edict_t *e = cl->edict;
		qboolean cargadores = false;

		if( cl->state != cs_spawned || !SV_IsValidEdict( e ))
		{
			sv_numvistos[i] = 0;
			continue;
		}

		for( j = 0; j < sv_numvariantes; j++ )
		{
			variante_t *v = &sv_variantes[j];

			if( !sv_elegida[i][j] )
				continue;
			if( e->v.viewmodel && Variantes_EnLista( v->v_base, SV_GetString( e->v.viewmodel )))
				e->v.viewmodel = sv_vstr[j];
			if( e->v.weaponmodel && Variantes_EnLista( v->p_base, SV_GetString( e->v.weaponmodel )))
				e->v.weaponmodel = sv_pstr[j];
			if( v->cargador_arma[0] )
			{
				SV_VariantesCargadores( i, e, v );
				cargadores = true;
			}
		}

		// what was seen means nothing once the variant is dropped
		if( !cargadores )
			sv_numvistos[i] = 0;
	}
}

/*
==============
SV_VariantesModelo

SetModel: what a player with a variant drops or throws (the game uses the
world model or the player model for it)
==============
*/
const char *SV_VariantesModelo( edict_t *ent, const char *modelname )
{
	const variante_t *v;
	edict_t *dueno;
	int i, slot;

	if( !sv_numvariantes || !SV_IsValidEdict( ent ))
		return modelname;

	dueno = ent->v.owner;
	if( !SV_IsValidEdict( dueno ))
		return modelname;

	slot = NUM_FOR_EDICT( dueno ) - 1;
	if( slot < 0 || slot >= svs.maxclients )
		return modelname;

	for( i = 0; i < sv_numvariantes; i++ )
	{
		v = &sv_variantes[i];
		if( !sv_elegida[slot][i] )
			continue;
		if( Variantes_EnLista( v->w_base, modelname ))
		{
			// thrown (a grenade): its speed changes once the game has set it
			if( v->lanzamiento != 1.0f && sv_numlanzados < ARRAYSIZE( sv_lanzados ))
			{
				sv_lanzados[sv_numlanzados] = NUM_FOR_EDICT( ent );
				sv_lanzados_f[sv_numlanzados++] = v->lanzamiento;
			}
			return v->w_nuevo;
		}
		if( Variantes_EnLista( v->p_base, modelname ))
			return v->p_nuevo;
	}
	return modelname;
}

/*
==============
SV_VariantesEnMano

the variant the player holds, if any
==============
*/
static const variante_t *SV_VariantesEnMano( edict_t *jugador )
{
	const char *p;
	int i, slot;

	if( !sv_numvariantes || !jugador->v.weaponmodel )
		return NULL;

	slot = NUM_FOR_EDICT( jugador ) - 1;
	if( slot < 0 || slot >= svs.maxclients )
		return NULL;

	p = SV_GetString( jugador->v.weaponmodel );
	for( i = 0; i < sv_numvariantes; i++ )
	{
		const variante_t *v = &sv_variantes[i];

		if( sv_elegida[slot][i] && ( !Q_stricmp( p, v->p_nuevo ) || Variantes_EnLista( v->p_base, p )))
			return v;
	}
	return NULL;
}

/*
==============
SV_VariantesBala

a trace asked for by a player: how far off the view the bullets of the
weapon in hand go and their damage, times the base weapon's
==============
*/
float SV_VariantesBala( edict_t *jugador, float *dano )
{
	const variante_t *v = SV_VariantesEnMano( jugador );

	*dano = v ? v->dano : 1.0f;
	return v ? v->precision : 1.0f;
}

/*
==============
SV_VariantesDano

inside pfnTraceLine (pila is its frame): if the game is firing bullets, their
damage times f, once for each FireBulletsPlayer
==============
*/
void SV_VariantesDano( float f, void *pila )
{
	static int	*ultimo, escrito;
	static double	hora;
	static void	*instancia;
	static uint	retorno;	// 0: not Firearms 3.0
	uint		*p = pila;
	int		i, *dano;

	// where the game dll is (behind metamod the engine has that one's): Firearms
	// 3.0's has GiveFnptrsToDll at a known place
	if( instancia != svgame.hInstance )
	{
		const byte *dll = NULL;

		instancia = svgame.hInstance;
		retorno = 0;
#if XASH_WIN32
		dll = (const byte *)GetModuleHandleA( "firearms.dll" );
#endif
		if( dll && !memcmp( dll + FA_GIVEFNPTRS, fa_givefnptrs, sizeof( fa_givefnptrs ))
			&& !memcmp( dll + FA_DISPARO_RETORNO, fa_disparo_codigo, sizeof( fa_disparo_codigo )))
			retorno = (uint)( dll + FA_DISPARO_RETORNO );
	}

	if( !retorno )
		return;

	for( i = 0; i < 64 && p[i] != retorno; i++ );
	if( i == 64 )
		return; // not a bullet

	dano = (int *)((byte *)&p[i] + FA_DISPARO_DANO );

	// the next traces of the same bullets (through walls, the other pellets)
	if( dano == ultimo && *dano == escrito && hora == sv.time )
		return;

	if( variantes_debug.value )
		Con_Printf( "[variantes] dano %d -> %d\n", *dano, (int)( *dano * f + 0.5f ));

	*dano = (int)( *dano * f + 0.5f );
	ultimo = dano;
	escrito = *dano;
	hora = sv.time;
}
