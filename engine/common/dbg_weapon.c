/*
dbg_weapon.c - dump the mod-visible weapon prediction state on change
Copyright (C) 2026 Xash3D FWGS contributors

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
#include "entity_state.h"
#include "weaponinfo.h"
#include "dbg_weapon.h"

// mods keep their per-player and per-weapon state in the user fields, so those
// are what we watch. everything here is sampled after the game library filled
// the struct, both on the server and on the client predictor, which lets us
// tell "the mod never set it" apart from "the engine lost it on the way"

typedef struct
{
	qboolean	valid;
	int		cd_iuser[4];
	float		cd_fuser[4];
	int		cd_tfstate;
	int		cd_bInDuck;
	int		cd_weaponanim;
	int		cd_m_iId;
	float		cd_view_ofs_z;
	float		cd_maxspeed;
	float		cd_fov;
	int		wd_m_iId;
	int		wd_m_iWeaponState;
	int		wd_m_fInZoom;
	float		wd_m_fAimedDamage;
	float		wd_m_fNextAimBonus;
	int		wd_iuser[4];
	float		wd_fuser[4];
	float		wd_next_primary;
	// these come straight from the server that filled clientdata_t, so they are
	// comparable across engines: connect to a GoldSrc server and the CL dump
	// shows what GoldSrc computed, not what we did
	vec3_t		cd_punchangle;
	float		cd_velocity;
	int		cd_flags;
} dbg_weapon_state_t;

static dbg_weapon_state_t dbg_weapon_last[2];

static CVAR_DEFINE_AUTO( dbg_weapondata, "0", 0, "dump clientdata_t/weapon_data_t user fields when they change" );

void DbgWeapon_Init( void )
{
	Cvar_RegisterVariable( &dbg_weapondata );
}

static qboolean DbgWeapon_Changed( const dbg_weapon_state_t *a, const dbg_weapon_state_t *b )
{
	if( !a->valid )
		return true;

	// bitwise compare is enough: both sides are filled from the same struct fields,
	// so a float that didn't change is bit-identical, and NaN would be worth seeing anyway
	return memcmp((const byte *)a + offsetof( dbg_weapon_state_t, cd_iuser ),
		(const byte *)b + offsetof( dbg_weapon_state_t, cd_iuser ),
		sizeof( *a ) - offsetof( dbg_weapon_state_t, cd_iuser )) != 0;
}

/*
=================
DbgWeapon_Report

side: DBG_WEAPON_SV or DBG_WEAPON_CL
wd may be NULL when the game library doesn't supply weapon prediction data
=================
*/
void DbgWeapon_Report( int side, const clientdata_t *cd, const weapon_data_t *wd )
{
	dbg_weapon_state_t cur;
	dbg_weapon_state_t *last;

	if( side < 0 || side >= ARRAYSIZE( dbg_weapon_last ))
		return;

	if( !dbg_weapondata.value )
	{
		dbg_weapon_last[side].valid = false;
		return;
	}

	memset( &cur, 0, sizeof( cur ));
	cur.valid = true;

	cur.cd_iuser[0] = cd->iuser1;
	cur.cd_iuser[1] = cd->iuser2;
	cur.cd_iuser[2] = cd->iuser3;
	cur.cd_iuser[3] = cd->iuser4;
	cur.cd_fuser[0] = cd->fuser1;
	cur.cd_fuser[1] = cd->fuser2;
	cur.cd_fuser[2] = cd->fuser3;
	cur.cd_fuser[3] = cd->fuser4;
	cur.cd_tfstate = cd->tfstate;
	cur.cd_bInDuck = cd->bInDuck;
	cur.cd_weaponanim = cd->weaponanim;
	cur.cd_m_iId = cd->m_iId;
	cur.cd_view_ofs_z = cd->view_ofs[2];
	cur.cd_maxspeed = cd->maxspeed;
	cur.cd_fov = cd->fov;
	VectorCopy( cd->punchangle, cur.cd_punchangle );
	cur.cd_velocity = VectorLength( cd->velocity );
	cur.cd_flags = cd->flags;

	if( wd )
	{
		cur.wd_m_iId = wd->m_iId;
		cur.wd_m_iWeaponState = wd->m_iWeaponState;
		cur.wd_m_fInZoom = wd->m_fInZoom;
		cur.wd_m_fAimedDamage = wd->m_fAimedDamage;
		cur.wd_m_fNextAimBonus = wd->m_fNextAimBonus;
		cur.wd_iuser[0] = wd->iuser1;
		cur.wd_iuser[1] = wd->iuser2;
		cur.wd_iuser[2] = wd->iuser3;
		cur.wd_iuser[3] = wd->iuser4;
		cur.wd_fuser[0] = wd->fuser1;
		cur.wd_fuser[1] = wd->fuser2;
		cur.wd_fuser[2] = wd->fuser3;
		cur.wd_fuser[3] = wd->fuser4;
		cur.wd_next_primary = wd->m_flNextPrimaryAttack;
	}

	last = &dbg_weapon_last[side];

	if( !DbgWeapon_Changed( last, &cur ))
		return;

	*last = cur;

	Con_Printf( "[%.2f wd:%s] cd: iu=%d,%d,%d,%d fu=%.3f,%.3f,%.3f,%.3f tf=%d duck=%d vofs=%.1f spd=%.1f fov=%.1f anim=%d wpn=%d\n",
		host.realtime, side == DBG_WEAPON_SV ? "SV" : "CL",
		cur.cd_iuser[0], cur.cd_iuser[1], cur.cd_iuser[2], cur.cd_iuser[3],
		cur.cd_fuser[0], cur.cd_fuser[1], cur.cd_fuser[2], cur.cd_fuser[3],
		cur.cd_tfstate, cur.cd_bInDuck, cur.cd_view_ofs_z, cur.cd_maxspeed,
		cur.cd_fov, cur.cd_weaponanim, cur.cd_m_iId );

	Con_Printf( "[%.2f wd:%s] mv: punch=%.2f,%.2f,%.2f vel=%.1f flags=0x%x\n",
		host.realtime, side == DBG_WEAPON_SV ? "SV" : "CL",
		cur.cd_punchangle[0], cur.cd_punchangle[1], cur.cd_punchangle[2],
		cur.cd_velocity, cur.cd_flags );

	if( wd )
	{
		Con_Printf( "[%.2f wd:%s] wd: id=%d state=%d zoom=%d aimdmg=%.3f aimbonus=%.3f iu=%d,%d,%d,%d fu=%.3f,%.3f,%.3f,%.3f\n",
			host.realtime, side == DBG_WEAPON_SV ? "SV" : "CL",
			cur.wd_m_iId, cur.wd_m_iWeaponState, cur.wd_m_fInZoom,
			cur.wd_m_fAimedDamage, cur.wd_m_fNextAimBonus,
			cur.wd_iuser[0], cur.wd_iuser[1], cur.wd_iuser[2], cur.wd_iuser[3],
			cur.wd_fuser[0], cur.wd_fuser[1], cur.wd_fuser[2], cur.wd_fuser[3] );

		Con_Printf( "[%.2f wd:%s] wd: next_primary=%.3f\n",
			host.realtime, side == DBG_WEAPON_SV ? "SV" : "CL", cur.wd_next_primary );
	}
}

/*
=================
DbgWeapon_Event

mods pass the per-shot spread and recoil to the client through the event
parameters, so at level 2 we log every event the game library plays back.
the same shot shows up twice when weapon prediction is on: once from the
client predictor and once from the server, which is exactly what we want
to compare
=================
*/
void DbgWeapon_Event( int side, const char *name, const float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 )
{
	static const vec3_t nullangles;

	if( dbg_weapondata.value < 2.0f )
		return;

	if( !angles )
		angles = nullangles;

	// angles is the aim direction the shot actually used, so its drift across a
	// burst is the muzzle climb the player feels as "accuracy"
	Con_Printf( "[%.2f wd:%s] ev %s: ang=%.3f,%.3f,%.3f f1=%.4f f2=%.4f i1=%d i2=%d b1=%d b2=%d\n",
		host.realtime, side == DBG_WEAPON_SV ? "SV" : "CL", name ? name : "?",
		angles[0], angles[1], angles[2],
		fparam1, fparam2, iparam1, iparam2, bparam1, bparam2 );
}

/*
=================
DbgWeapon_ForcedAngle

view angle changes pushed by the server (svc_setangle / svc_addangle). unlike
the angles carried in a fire event these contain no mouse input at all, so they
are the clean measure of what the mod does to your aim
=================
*/
void DbgWeapon_ForcedAngle( const char *kind, float a, float b, float c )
{
	if( !dbg_weapondata.value )
		return;

	Con_Printf( "[%.2f wd:CL] forced %s: %.3f %.3f %.3f\n", host.realtime, kind, a, b, c );
}

/*
=================
DbgWeapon_Clock

the clock the server hands to the game library. mods decrement their weapon
timers by gpGlobals->frametime once per PostThink, so if this does not advance
at real time every timer-driven mechanic (accuracy recovery included) drifts
=================
*/
void DbgWeapon_Clock( double gtime, double frametime, double svtime, double timebase )
{
	static double last_gtime, last_real;

	if( dbg_weapondata.value < 3.0f )
		return;

	Con_Printf( "[%.2f wd:SV] clock: gtime=%.4f d_gtime=%.4f frametime=%.4f sv.time=%.4f timebase=%.4f d_real=%.4f\n",
		host.realtime, gtime, gtime - last_gtime, frametime, svtime, timebase, host.realtime - last_real );

	last_gtime = gtime;
	last_real = host.realtime;
}

/*
=================
DbgWeapon_Received

what the server actually put on the wire, sampled before the client predictor
touches it. the CL lines from DbgWeapon_Report are post-prediction, so only
this one is comparable against the SV lines
=================
*/
void DbgWeapon_Received( const clientdata_t *cd, const weapon_data_t *wd )
{
	if( dbg_weapondata.value < 2.0f )
		return;

	Con_Printf( "[%.2f wd:RX] cd: iu=%d,%d,%d,%d fu=%.3f,%.3f,%.3f,%.3f | wd: id=%d state=%d aimbonus=%.4f next=%.4f iu=%d,%d,%d,%d fu=%.4f,%.4f,%.4f,%.4f\n",
		host.realtime,
		cd->iuser1, cd->iuser2, cd->iuser3, cd->iuser4,
		cd->fuser1, cd->fuser2, cd->fuser3, cd->fuser4,
		wd->m_iId, wd->m_iWeaponState, wd->m_fNextAimBonus, wd->m_flNextPrimaryAttack,
		wd->iuser1, wd->iuser2, wd->iuser3, wd->iuser4,
		wd->fuser1, wd->fuser2, wd->fuser3, wd->fuser4 );
}
