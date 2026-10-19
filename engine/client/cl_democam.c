/*
cl_democam.c - follow any player while a demo plays back
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
#include "pm_defs.h"
#include "keydefs.h"
#include "studio.h"
#include "platform/platform.h"

/*
==============================================================================
DEMO CAMERA

A demo only stores what the recording client was sent, so it holds every
player when the server ran with sv_novis 1. While it plays back, the camera
can leave the recorded view and follow any of them, the way HLTV does it
but without help from client.dll (old ones like CS 1.3 have no spectator code):

  left click  - next player
  right click - previous player
  space       - first person -> third person -> free view
  mouse       - third person and free view: turns the camera
  wheel       - first person: zooms in or out; third person: nearer or further
  movement    - free view: forward, back and sideways where it looks, use
                up, duck down, speed (shift) faster. Through walls

First person on whoever recorded the demo is the recorded view itself.
==============================================================================
*/
enum
{
	DEMOCAM_FIRSTPERSON = 0,
	DEMOCAM_CHASE,
	DEMOCAM_FREE, // flies around the map like a spectator
	DEMOCAM_MODES
};

#define DEMOCAM_VIEW_HEIGHT       17.0f // VEC_VIEW in CS
#define DEMOCAM_DUCK_VIEW_HEIGHT  12.0f
#define DEMOCAM_CHASE_DISTANCE    96.0f
#define DEMOCAM_CHASE_MIN         32.0f
#define DEMOCAM_CHASE_MAX         512.0f
#define DEMOCAM_CHASE_HEIGHT      16.0f
#define DEMOCAM_FRAME_NOISE       4.0f
#define DEMOCAM_ZOOM_STEP         1.25f
#define DEMOCAM_ZOOM_MAX          8.0f
#define DEMOCAM_FREE_SPEED        400.0f // units per second
#define DEMOCAM_FREE_FAST         3.0f   // with speed held

static int democam_mode;
static int democam_target; // entity index, 1..maxclients, 0 for whoever recorded it

// the third person camera looks wherever the mouse turns it
static vec3_t   democam_chase_angles;
static qboolean democam_chase_aimed;
static float    democam_chase_distance = DEMOCAM_CHASE_DISTANCE;

// first person zoom, towards the middle of the screen
static float    democam_zoom = 1.0f;

// the free view, starts where the last view was
static vec3_t   democam_free_org, democam_free_angles;
static qboolean democam_free_placed;
static vec3_t   democam_last_org, democam_last_angles;

/*
the demo can't show a silencer being put on or taken off, that animation
only plays for whoever does it, but every shot says whether it was there:
CS sends it as one of the boolean parameters of the weapon event
*/
static const struct
{
	const char *event;
	const char *viewmodel;
	int        bparam;
} democam_silencers[] =
{
	{ "events/m4a1.sc", "v_m4a1", 1 },
	{ "events/usp.sc",  "v_usp",  2 },
};

static qboolean democam_silenced[MAX_CLIENTS + 1][ARRAYSIZE( democam_silencers )];

// the HUD of the followed player, see CL_DemoCamUserMsg
#define DEMOCAM_STAT_ENTRIES 48
#define DEMOCAM_STAT_DATA    16
#define DEMOCAM_STAT_MONEY   3
#define DEMOCAM_STAT_AMMOX   5 // one kept per ammo type
#define DEMOCAM_STAT_SETFOV  6 // the zoom, handed over by CL_DemoCamFovFrame
#define DEMOCAM_STAT_FADE    7 // a flashbang going off, not kept

static CVAR_DEFINE_AUTO( cl_demostats, "1", FCVAR_USERINFO, "ask the server for everyone's health, armor, ammo and money, so demos have them" );

// same order as the server
static const char *democam_stat_names[] = { "Health", "Battery", "ArmorType", "Money", "CurWeapon", "AmmoX", "SetFOV", "ScreenFade" };

typedef struct
{
	byte kind;
	byte len;
	byte data[DEMOCAM_STAT_DATA];
} democam_stat_t;

static struct
{
	democam_stat_t stats[DEMOCAM_STAT_ENTRIES];
	int            num;
} democam_stats[MAX_CLIENTS + 1];

static int democam_hud; // whose numbers client.dll has: 0 the recorder's, or the player followed

/*
the zoom of the recorder's weapon (SetFOV) is the engine's to hand to
client.dll: CS draws the fixed crosshair of a zoomed AUG or SG552 from it,
and it only changes the crosshair on the CurWeapon that comes after. Going
back in the demo, or following someone else while the recorder zooms, left
the fixed crosshair on. Every SetFOV is kept with its time, the first time
it is read, and client.dll gets the one of the moment shown, or none (90)
while the camera is on someone else
*/
#define DEMOCAM_MAX_FOVS 4096
#define DEMOCAM_STAT_CURWEAPON 4

static struct
{
	float time;
	byte  fov;
} democam_fovs[DEMOCAM_MAX_FOVS];
static int  democam_numfovs;
static char democam_fov_demo[MAX_QPATH];
static int  democam_given_fov = -1; // what client.dll has, -1 not known

/*
besides SetFOV, client.dll keeps the recorder's zoom in its own blocks of
the demo (dem_userdata, read by its Demo_ReadBuffer): an int 1 and the fov
as a float, every frame. It took them following anyone, so the recorder's
AWP zoomed the view of the others, with the AUG crosshair on top
*/
#define DEMOCAM_DEMOZOOM_TYPE 1

static float    democam_demozoom = 90.0f;  // the recorder's, from the last block
static qboolean democam_has_demozoom;      // this demo brings them
static float    democam_given_demozoom = -1.0f; // what client.dll has, -1 not known

/*
CS picks the crosshair on CurWeapon from the zoom it already has, which
comes from those blocks and not from SetFOV, a frame or more apart: given
on SetFOV, the AWP got the cross inside the scope one zoom late. It's given
again when the zoom client.dll reports (scr_fov) changes
*/
static int democam_crosshair_fov = -1;

// the weapons client.dll knows about, sent once when joining: without them it shows no ammo
#define DEMOCAM_WEAPONLIST     32
#define DEMOCAM_WEAPONLIST_LEN 64

static struct
{
	byte data[DEMOCAM_WEAPONLIST_LEN];
	int  len;
} democam_weaponlist[DEMOCAM_WEAPONLIST];
static int democam_numweaponlist;

static qboolean CL_DemoCamValidTarget( int index )
{
	cl_entity_t *ent;

	if( index < 1 || index > cl.maxclients )
		return false;

	ent = CL_GetEntityByIndex( index );

	// dead CS players turn into invisible observers
	if( !ent || !ent->model || ent->curstate.messagenum != cl.parsecount )
		return false;

	return !FBitSet( ent->curstate.effects, EF_NODRAW );
}

// the player the camera is on, whoever recorded the demo unless another was picked
static int CL_DemoCamCurrent( void )
{
	if( democam_target < 1 || democam_target > cl.maxclients )
		return cl.playernum + 1;

	return democam_target;
}

// first person on whoever recorded the demo: his own view
static qboolean CL_DemoCamRecorded( void )
{
	return democam_mode == DEMOCAM_FIRSTPERSON && CL_DemoCamCurrent() == cl.playernum + 1;
}

static void CL_DemoCamResetZoom( void )
{
	democam_zoom = 1.0f;
}

/*
=================
CL_DemoCamFree

true in the free view: on nobody, the crosshair would only confuse
=================
*/
qboolean CL_DemoCamFree( void )
{
	return cls.demoplayback && !cl.background && democam_mode == DEMOCAM_FREE;
}

/*
=================
CL_DemoCamOnOther

the camera was put on someone other than whoever recorded the demo, or
in the free view. Unlike CL_DemoCamActive, also while he respawns for a
moment: that's when the recorder gets his pistol and knife
=================
*/
qboolean CL_DemoCamOnOther( void )
{
	if( !cls.demoplayback || cl.background )
		return false;

	return democam_mode == DEMOCAM_FREE || CL_DemoCamCurrent() != cl.playernum + 1;
}

/*
=================
CL_DemoCamListener

whose sounds are heard from the inside: the player followed, nobody's in
the free view (0, the world), else whoever recorded the demo
=================
*/
int CL_DemoCamListener( int viewentity )
{
	if( !CL_DemoCamOnOther( ))
		return viewentity;

	return democam_mode == DEMOCAM_FREE ? 0 : CL_DemoCamCurrent();
}

/*
=================
CL_DemoCamSkipSound

following someone else, the recorder picking up what he bought or got at
the start of the round isn't heard, nor what client.dll plays on its own
for him (reloading, HUD)
=================
*/
qboolean CL_DemoCamSkipSound( int entnum, int channel )
{
	return channel == CHAN_ITEM && entnum == cl.playernum + 1 && CL_DemoCamOnOther( );
}

static void CL_DemoCamCycle( int dir )
{
	int i, index = CL_DemoCamCurrent();

	for( i = 0; i < cl.maxclients; i++ )
	{
		index += dir;

		if( index > cl.maxclients )
			index = 1;
		else if( index < 1 )
			index = cl.maxclients;

		if( CL_DemoCamValidTarget( index ) || index == cl.playernum + 1 )
		{
			democam_target = index;
			democam_chase_aimed = false;
			CL_DemoCamResetZoom();
			return;
		}
	}
}

/*
=================
CL_DemoCamActive

true while the camera shows someone other than the recorded view
=================
*/
qboolean CL_DemoCamActive( void )
{
	if( !cls.demoplayback || cl.background || CL_DemoCamRecorded( ))
		return false;

	if( democam_mode == DEMOCAM_FREE )
		return true;

	return CL_DemoCamValidTarget( CL_DemoCamCurrent( ));
}

/*
=================
CL_DemoCamHideEntity

the followed player is not drawn in first person, we're inside his head
=================
*/
/*
=================
CL_DemoCamTarget

the player the camera follows, 0 on the recorded view
=================
*/
int CL_DemoCamTarget( void )
{
	return CL_DemoCamActive() && democam_mode != DEMOCAM_FREE ? CL_DemoCamCurrent() : 0;
}

qboolean CL_DemoCamFirstPerson( void )
{
	return democam_mode == DEMOCAM_FIRSTPERSON && CL_DemoCamActive();
}

qboolean CL_DemoCamHideEntity( const cl_entity_t *ent )
{
	return CL_DemoCamFirstPerson() && ent->index == CL_DemoCamCurrent();
}

static qboolean CL_DemoCamPlaying( void )
{
	return cls.demoplayback && !cl.background && cls.state == ca_active;
}

static void CL_DemoCamMode_f( void )
{
	if( !CL_DemoCamPlaying( ))
		return;

	democam_mode = ( democam_mode + 1 ) % DEMOCAM_MODES;
	democam_chase_aimed = false;
	democam_free_placed = false;
	CL_DemoCamResetZoom();
}

static void CL_DemoCamFollow( int dir )
{
	if( !CL_DemoCamPlaying( ))
		return;

	// from the free view, into someone's eyes
	if( democam_mode == DEMOCAM_FREE )
		democam_mode = DEMOCAM_FIRSTPERSON;

	CL_DemoCamCycle( dir );
}

static void CL_DemoCamNext_f( void )
{
	CL_DemoCamFollow( 1 );
}

static void CL_DemoCamPrev_f( void )
{
	CL_DemoCamFollow( -1 );
}

static void CL_DemoCamZoomBy( float scale )
{
	democam_zoom = bound( 1.0f, democam_zoom * scale, DEMOCAM_ZOOM_MAX );
}

static void CL_DemoCamZoom_f( void )
{
	if( !CL_DemoCamPlaying() || democam_mode != DEMOCAM_FIRSTPERSON || Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "democam_zoom <factor> (in first person; above 1 zooms in, below 1 out)\n" );
		return;
	}

	CL_DemoCamZoomBy( Q_atof( Cmd_Argv( 1 )));
}

/*
=================
CL_DemoCamZoom

the field of view, narrower when zoomed in in first person
=================
*/
void CL_DemoCamZoom( float *fov_x, float *fov_y )
{
	if( !CL_DemoCamPlaying() || democam_mode != DEMOCAM_FIRSTPERSON || democam_zoom <= 1.0f )
		return;

	*fov_x = RAD2DEG( atan( tan( DEG2RAD( *fov_x ) * 0.5f ) / democam_zoom )) * 2.0f;
	*fov_y = RAD2DEG( atan( tan( DEG2RAD( *fov_y ) * 0.5f ) / democam_zoom )) * 2.0f;
}

/*
=================
CL_DemoCamKey

takes the mouse buttons and space while a demo plays
=================
*/
qboolean CL_DemoCamKey( int key, qboolean down )
{
	const char *binding;

	if( !CL_DemoCamPlaying() || cls.key_dest != key_game )
		return false;

	// no chatting from a demo
	binding = Key_GetBinding( key );
	if( binding && ( !Q_stricmp( binding, "messagemode" ) || !Q_stricmp( binding, "messagemode2" )))
		return true;

	// the wheel is the camera's: the weapon menu of client.dll has nothing to do here
	if( key == K_MWHEELUP || key == K_MWHEELDOWN )
	{
		if( democam_mode == DEMOCAM_FIRSTPERSON )
		{
			if( down )
				CL_DemoCamZoomBy( key == K_MWHEELUP ? DEMOCAM_ZOOM_STEP : 1.0f / DEMOCAM_ZOOM_STEP );
			return true;
		}

		if( democam_mode != DEMOCAM_CHASE || !CL_DemoCamActive( ))
			return true;

		if( down )
		{
			democam_chase_distance *= key == K_MWHEELUP ? 0.85f : 1.0f / 0.85f;
			democam_chase_distance = bound( DEMOCAM_CHASE_MIN, democam_chase_distance, DEMOCAM_CHASE_MAX );
		}

		return true;
	}

	if( key != K_MOUSE1 && key != K_MOUSE2 && key != K_SPACE )
		return false;

	if( !down )
		return true;

	if( key == K_SPACE )
		CL_DemoCamMode_f();
	else CL_DemoCamFollow( key == K_MOUSE1 ? 1 : -1 );

	return true;
}

void CL_DemoCamInit( void )
{
	Cmd_AddCommand( "democam_next", CL_DemoCamNext_f, "demo playback: follow the next player" );
	Cmd_AddCommand( "democam_prev", CL_DemoCamPrev_f, "demo playback: follow the previous player" );
	Cmd_AddCommand( "democam_mode", CL_DemoCamMode_f, "demo playback: first person or third person" );
	Cmd_AddCommand( "democam_zoom", CL_DemoCamZoom_f, "demo playback, first person: zoom in (above 1) or out" );
	Cvar_RegisterVariable( &cl_demostats );
}

/*
=================
CL_DemoCamMouseFrame

client.dll doesn't read the mouse while a demo plays (CS 1.3 reads it
with the Windows API, and only when it moves the player), so the engine
does: where the pointer went since the last frame, then back to the
middle of the window so it never stops at an edge
=================
*/
void CL_DemoCamMouseFrame( void )
{
	static qboolean centered;
	float scale_x = refState.scale_x > 0.0f ? refState.scale_x : 1.0f;
	float scale_y = refState.scale_y > 0.0f ? refState.scale_y : 1.0f;
	int x, y, cx = refState.width / 2, cy = refState.height / 2;
	float dx, dy, *angles;
	qboolean want;

	want = cls.demoplayback && !cl.background && cls.key_dest == key_game && host.status == HOST_FRAME
		&& (( democam_mode == DEMOCAM_FREE && democam_free_placed ) || ( democam_mode == DEMOCAM_CHASE && democam_chase_aimed ))
		&& CL_DemoCamActive( );

	if( !want )
	{
		centered = false;
		return;
	}

	Platform_GetMousePos( &x, &y );
	Platform_SetMousePos( cx / scale_x, cy / scale_y );

	// the first time it wasn't in the middle yet
	if( !centered )
	{
		centered = true;
		return;
	}

	// turns the camera like it would turn the player
	dx = ( x - cx ) * Cvar_VariableValue( "sensitivity" ) * Cvar_VariableValue( "m_yaw" );
	dy = ( y - cy ) * Cvar_VariableValue( "sensitivity" ) * Cvar_VariableValue( "m_pitch" );

	angles = democam_mode == DEMOCAM_FREE ? democam_free_angles : democam_chase_angles;
	angles[YAW] = anglemod( angles[YAW] - dx );
	angles[PITCH] = bound( -89.0f, angles[PITCH] + dy, 89.0f );
}

/*
=================
CL_DemoCamReset

every demo starts on the recorded view
=================
*/
void CL_DemoCamReset( void )
{
	// started again only to go back in time, keep following the same player
	if( CL_DemoCtlRestarting( ))
		return;

	democam_mode = DEMOCAM_FIRSTPERSON;
	democam_target = 0;
	democam_chase_aimed = false;
	democam_free_placed = false;
	CL_DemoCamResetZoom();
	memset( democam_silenced, 0, sizeof( democam_silenced ));
}

/*
==============================================================================
HUD OF THE FOLLOWED PLAYER

The game sends health, armor, ammo and money only to their owner. A
custom-xash server copies everyone's to clients with cl_demostats on
(XDStats, see sv_game.c), so they end up in the demo. While following a
player, client.dll gets his instead of the recorder's, and its own HUD
shows them; the recorder's are kept aside and given back on his view.
Demos without them keep showing the recorder's.
==============================================================================
*/
// a new server or level, or a demo starting
void CL_DemoCamClearStats( void )
{
	democam_hud = 0;
	democam_given_fov = -1;
	democam_demozoom = 90.0f;
	democam_has_demozoom = false;
	democam_given_demozoom = -1.0f;
	democam_crosshair_fov = -1;
	memset( democam_stats, 0, sizeof( democam_stats ));
	democam_numweaponlist = 0;
}

// one per weapon, told apart by its name at the start
static void CL_DemoCamStoreWeaponList( const byte *buf, int size )
{
	int i;

	if( size <= 0 || size > DEMOCAM_WEAPONLIST_LEN )
		return;

	for( i = 0; i < democam_numweaponlist; i++ )
	{
		if( !Q_strncmp( (const char *)democam_weaponlist[i].data, (const char *)buf, size ))
			break;
	}

	if( i == democam_numweaponlist )
	{
		if( democam_numweaponlist >= DEMOCAM_WEAPONLIST )
			return;

		democam_numweaponlist++;
	}

	memcpy( democam_weaponlist[i].data, buf, size );
	democam_weaponlist[i].len = size;
}

static void CL_DemoCamStore( int player, int kind, const byte *data, int len )
{
	democam_stat_t *st = NULL;
	int i;

	if( player < 1 || player > MAX_CLIENTS || len <= 0 || len > DEMOCAM_STAT_DATA )
		return;

	// one comes for every weapon carried: only the one in hand (or none, id 0) picks the crosshair
	if( kind == DEMOCAM_STAT_CURWEAPON && len >= 2 && data[0] == 0 && data[1] != 0 )
		return;

	for( i = 0; i < democam_stats[player].num; i++ )
	{
		democam_stat_t *it = &democam_stats[player].stats[i];

		if( it->kind == kind && ( kind != DEMOCAM_STAT_AMMOX || it->data[0] == data[0] ))
		{
			st = it;
			break;
		}
	}

	if( !st )
	{
		if( democam_stats[player].num >= DEMOCAM_STAT_ENTRIES )
			return;

		st = &democam_stats[player].stats[democam_stats[player].num++];
	}

	st->kind = kind;
	st->len = len;
	memcpy( st->data, data, len );
}

// hands one to client.dll, as if the server had sent it
static void CL_DemoCamGiveHud( int kind, const byte *data, int len, qboolean replay )
{
	byte buf[DEMOCAM_STAT_DATA];
	int i;

	// the zoom goes with the view, see CL_DemoCamFovFrame
	if( kind == DEMOCAM_STAT_SETFOV )
		return;

	memcpy( buf, data, len );

	// the money doesn't flash for a value it already had
	if( replay && kind == DEMOCAM_STAT_MONEY && len >= 5 )
		buf[4] = 0;

	for( i = 0; i < MAX_USER_MESSAGES; i++ )
	{
		if( clgame.msg[i].func && !Q_strcmp( clgame.msg[i].name, democam_stat_names[kind] ))
		{
			clgame.msg[i].func( clgame.msg[i].name, len, buf );
			return;
		}
	}
}

static int CL_DemoCamStatKind( const char *name )
{
	int i;

	for( i = 0; i < ARRAYSIZE( democam_stat_names ); i++ )
	{
		if( !Q_strcmp( name, democam_stat_names[i] ))
			return i;
	}

	return -1;
}

/*
=================
CL_DemoCamUserMsg

every user message comes here first. True when client.dll mustn't get it
=================
*/
qboolean CL_DemoCamUserMsg( const char *name, const byte *buf, int size )
{
	int kind;

	// someone else's, copied by the server
	if( !Q_strcmp( name, "XDStats" ))
	{
		if( size < 3 || buf[1] >= ARRAYSIZE( democam_stat_names ))
			return true;

		// his flashbang, seen only from his eyes and once
		if( buf[1] == DEMOCAM_STAT_FADE )
		{
			if( cls.demoplayback && !CL_DemoCtlSeeking() && democam_mode == DEMOCAM_FIRSTPERSON
				&& CL_DemoCamOnOther() && CL_DemoCamCurrent() == buf[0] )
				CL_DemoCamGiveHud( buf[1], buf + 2, size - 2, false );
			return true;
		}

		CL_DemoCamStore( buf[0], buf[1], buf + 2, size - 2 );

		if( cls.demoplayback && democam_hud == buf[0] )
			CL_DemoCamGiveHud( buf[1], buf + 2, size - 2, false );

		return true;
	}

	// what the recorder bought or picked up, listed on the right: not someone else's
	if( CL_DemoCamOnOther() && ( !Q_strcmp( name, "WeapPickup" )
		|| !Q_strcmp( name, "AmmoPickup" ) || !Q_strcmp( name, "ItemPickup" )))
		return true;

	// the recorder's flashbangs blind only his view
	if( CL_DemoCamOnOther() && !Q_strcmp( name, "ScreenFade" ))
		return true;

	// text menus (PODBot's, the radio) were for whoever recorded, not to watch
	if( cls.demoplayback && !cl.background && !Q_strcmp( name, "ShowMenu" ))
		return true;

	// client.dll goes back to no zoom on its own
	if( !Q_strcmp( name, "ResetHUD" ) || !Q_strcmp( name, "InitHUD" ))
	{
		democam_given_fov = -1;
		return false;
	}

	// the recorder's zoom, kept and handed over by CL_DemoCamFovFrame
	if( cls.demoplayback && !cl.background && !Q_strcmp( name, "SetFOV" ) && size >= 1 )
	{
		float now = CL_DemoGetTime();

		if( Q_stricmp( democam_fov_demo, cls.demoname ))
		{
			Q_strncpy( democam_fov_demo, cls.demoname, sizeof( democam_fov_demo ));
			democam_numfovs = 0;
		}

		// read again after going back, it's already there
		if(( democam_numfovs == 0 || now > democam_fovs[democam_numfovs - 1].time ) && democam_numfovs < DEMOCAM_MAX_FOVS )
		{
			democam_fovs[democam_numfovs].time = now;
			democam_fovs[democam_numfovs].fov = buf[0];
			democam_numfovs++;
		}
		else if( democam_numfovs > 0 && now == democam_fovs[democam_numfovs - 1].time )
			democam_fovs[democam_numfovs - 1].fov = buf[0];

		return true;
	}

	if( !Q_strcmp( name, "WeaponList" ))
	{
		CL_DemoCamStoreWeaponList( buf, size );
		return false;
	}

	// the recorder's flashbang, on his view: client.dll's at once, nothing to keep
	if(( kind = CL_DemoCamStatKind( name )) < 0 || kind == DEMOCAM_STAT_FADE )
		return false;

	// the recorder's own: kept aside while client.dll shows someone else's
	CL_DemoCamStore( cl.playernum + 1, kind, buf, size );

	return cls.demoplayback && democam_hud != 0;
}

static const cl_user_message_t *CL_DemoCamFindMsg( const char *name )
{
	int i;

	for( i = 0; i < MAX_USER_MESSAGES; i++ )
	{
		if( clgame.msg[i].number && !Q_strcmp( clgame.msg[i].name, name ))
			return &clgame.msg[i];
	}

	return NULL;
}

/*
=================
CL_DemoCamWriteStats

a recording started: the game only sends these when they change, so the
demo gets the last ones of everybody, as if they had just come
=================
*/
void CL_DemoCamWriteStats( sizebuf_t *buf )
{
	const cl_user_message_t *xds = CL_DemoCamFindMsg( "XDStats" );
	const cl_user_message_t *wl = CL_DemoCamFindMsg( "WeaponList" );
	int player, i;

	// demos of GoldSrc servers have the size of user messages as a byte
	if( cls.net_protocol == PROTO_GOLDSRC )
		return;

	for( i = 0; wl && i < democam_numweaponlist; i++ )
	{
		if( wl->size != -1 && wl->size != democam_weaponlist[i].len )
			continue;

		MSG_BeginServerCmd( buf, wl->number );
		if( wl->size == -1 )
			MSG_WriteWord( buf, democam_weaponlist[i].len );
		MSG_WriteBytes( buf, democam_weaponlist[i].data, democam_weaponlist[i].len );
	}

	for( player = 1; player <= cl.maxclients; player++ )
	{
		for( i = 0; i < democam_stats[player].num; i++ )
		{
			const democam_stat_t *st = &democam_stats[player].stats[i];
			const cl_user_message_t *msg;

			if( player == cl.playernum + 1 )
			{
				if( !( msg = CL_DemoCamFindMsg( democam_stat_names[st->kind] )))
					continue;

				// fixed sizes don't write it
				if( msg->size != -1 && msg->size != st->len )
					continue;

				MSG_BeginServerCmd( buf, msg->number );
				if( msg->size == -1 )
					MSG_WriteWord( buf, st->len );
				MSG_WriteBytes( buf, st->data, st->len );
			}
			else if( xds )
			{
				MSG_BeginServerCmd( buf, xds->number );
				MSG_WriteWord( buf, st->len + 2 );
				MSG_WriteByte( buf, player );
				MSG_WriteByte( buf, st->kind );
				MSG_WriteBytes( buf, st->data, st->len );
			}
		}
	}
}

// the recorder's zoom at that moment, 0 for none
static int CL_DemoCamFovAt( float time )
{
	int i;

	if( Q_stricmp( democam_fov_demo, cls.demoname ))
		return 0;

	for( i = democam_numfovs - 1; i >= 0; i-- )
	{
		if( democam_fovs[i].time <= time )
			return democam_fovs[i].fov;
	}

	return 0;
}

// the camera shows what was recorded: the recorder, in first person
static qboolean CL_DemoCamRecordedView( void )
{
	return democam_mode == DEMOCAM_FIRSTPERSON && !CL_DemoCamOnOther( );
}

/*
=================
CL_DemoCamFollowedFov

the zoom of the player followed in first person, copied by the server
(XDStats), 0 for none
=================
*/
static int CL_DemoCamFollowedFov( void )
{
	int player = CL_DemoCamCurrent(), fov = 0, i;

	if( democam_mode != DEMOCAM_FIRSTPERSON || !CL_DemoCamOnOther( ))
		return 0;

	if( player < 1 || player > MAX_CLIENTS || player == cl.playernum + 1 )
		return 0;

	for( i = 0; i < democam_stats[player].num; i++ )
	{
		const democam_stat_t *st = &democam_stats[player].stats[i];

		if( st->kind == DEMOCAM_STAT_SETFOV )
			fov = st->data[0];

		// a dead one isn't looking through anything
		else if( st->kind == 0 && st->data[0] == 0 )
			return 0;
	}

	return fov == 90 ? 0 : fov;
}

// the zoom client.dll should have from those blocks
static float CL_DemoCamWantedDemoZoom( void )
{
	int fov;

	if( CL_DemoCamRecordedView( ))
		return democam_demozoom;

	fov = CL_DemoCamFollowedFov();
	return fov ? fov : 90.0f;
}

/*
=================
CL_DemoCamDemoBuffer

a block of client.dll's from the demo, about to be given to it: the
recorder's zoom only on the recorded view, the followed player's on his
=================
*/
void CL_DemoCamDemoBuffer( byte *buf, int size )
{
	int type;

	if( !cls.demoplayback || cl.background || size != 8 )
		return;

	memcpy( &type, buf, sizeof( type ));
	if( type != DEMOCAM_DEMOZOOM_TYPE )
		return;

	memcpy( &democam_demozoom, buf + 4, sizeof( democam_demozoom ));
	democam_has_demozoom = true;
	democam_given_demozoom = CL_DemoCamWantedDemoZoom();
	memcpy( buf + 4, &democam_given_demozoom, sizeof( democam_given_demozoom ));
}

// changing view, or the followed player zooming, gets no block from the demo: one is made
static void CL_DemoCamDemoZoomFrame( void )
{
	float fov = CL_DemoCamWantedDemoZoom();
	int type = DEMOCAM_DEMOZOOM_TYPE;
	byte buf[8];

	if( !democam_has_demozoom || fov == democam_given_demozoom || !clgame.hInstance )
		return;

	democam_given_demozoom = fov;
	memcpy( buf, &type, sizeof( type ));
	memcpy( buf + 4, &fov, sizeof( fov ));
	clgame.dllFuncs.pfnDemo_ReadBuffer( sizeof( buf ), buf );
}

/*
=================
CL_DemoCamFovFrame

client.dll gets the zoom of the moment, and the crosshair that goes with it
=================
*/
static void CL_DemoCamFovFrame( void )
{
	int want = 0, fov_now = (int)cl.local.scr_fov, player, i;

	CL_DemoCamDemoZoomFrame();

	// the recorder's on the recorded view, the followed player's on his
	if( CL_DemoCamRecordedView( ))
		want = CL_DemoCamFovAt( CL_DemoCtlNow( ));
	else want = CL_DemoCamFollowedFov();

	// 0 and 90 are both no zoom
	if( want == 90 )
		want = 0;

	if( want == democam_given_fov && fov_now == democam_crosshair_fov )
		return;

	democam_crosshair_fov = fov_now;

	if( want != democam_given_fov )
	{
		democam_given_fov = want;

		for( i = 0; i < MAX_USER_MESSAGES; i++ )
		{
			if( clgame.msg[i].func && !Q_strcmp( clgame.msg[i].name, "SetFOV" ))
			{
				byte fov = want;

				clgame.msg[i].func( clgame.msg[i].name, 1, &fov );
				break;
			}
		}
	}

	// the crosshair only changes with the weapon
	player = democam_hud ? democam_hud : cl.playernum + 1;

	for( i = 0; i < democam_stats[player].num; i++ )
	{
		const democam_stat_t *st = &democam_stats[player].stats[i];

		if( st->kind == DEMOCAM_STAT_CURWEAPON )
			CL_DemoCamGiveHud( st->kind, st->data, st->len, true );
	}
}

/*
=================
CL_DemoCamHudFrame

client.dll gets the numbers of whoever the camera is on
=================
*/
static void CL_DemoCamHudFrame( void )
{
	int want = 0, target = 0, player, i;

	// the one picked, also while he respawns; the free view is on nobody: the recorder's
	if( CL_DemoCamOnOther() && democam_mode != DEMOCAM_FREE )
		target = CL_DemoCamCurrent();

	if( target && target != cl.playernum + 1 && democam_stats[target].num > 0 )
		want = target;

	if( want == democam_hud )
		return;

	democam_hud = want;
	player = want ? want : cl.playernum + 1;

	for( i = 0; i < democam_stats[player].num; i++ )
	{
		const democam_stat_t *st = &democam_stats[player].stats[i];

		CL_DemoCamGiveHud( st->kind, st->data, st->len, true );
	}
}

static void CL_DemoCamDrawInfo( void )
{
	con_nprint_t line = { .time_to_live = 0.1f, .color = { 1.0f, 0.7f, 0.1f }};
	const char *name;
	int index = CL_DemoCamCurrent();

	line.index = 1;

	if( democam_mode == DEMOCAM_FREE )
	{
		Con_NXPrintf( &line, "Vista libre  -  raton: mirar  -  WASD: moverse  -  E/Ctrl: subir/bajar  -  Mayus: rapido  -  clic: seguir a un jugador  -  espacio: primera persona" );
		return;
	}

	// someone else picked, who isn't there now (dead): the recorded view shows
	if( !CL_DemoCamRecorded() && !CL_DemoCamActive( ))
	{
		Con_NXPrintf( &line, "Vista grabada  -  clic izq/der: seguir a otro jugador" );
		return;
	}

	name = cl.players[index - 1].name;

	// demos recorded before CL_WriteDemoPlayers have no names for those already playing
	if( COM_StringEmpty( name ))
		name = va( "jugador %i", index );

	{
		static const char *modes[DEMOCAM_MODES] = { "primera persona, rueda: zoom", "tercera persona, raton y rueda", "" };
		static const char *next[DEMOCAM_MODES] = { "tercera persona", "vista libre", "" };

		Con_NXPrintf( &line, "Siguiendo a %s (%s)  -  clic izq/der: cambiar  -  espacio: %s",
			name, modes[democam_mode], next[democam_mode] );
	}
}

/*
=================
CL_DemoCamViewModel

the demo only knows the weapon the followed player carries on his model
(models/p_ak47.mdl), the one in his hands is its v_ version
=================
*/
static model_t *CL_DemoCamViewModel( const cl_entity_t *ent, int *index )
{
	model_t *pmodel = CL_ModelHandle( ent->curstate.weaponmodel );
	const char *base;
	char vname[MAX_QPATH];
	int i, j;

	if( !pmodel )
		return NULL;

	base = COM_FileWithoutPath( pmodel->name );

	if( Q_strnicmp( base, "p_", 2 ))
		return NULL;

	// CS 1.3 has the right handed ones apart, v_ak47_r.mdl, and uses them unless told lefthand
	for( j = 0; j < 2; j++ )
	{
		Q_snprintf( vname, sizeof( vname ), "%.*sv_%s", (int)( base - pmodel->name ), pmodel->name, base + 2 );

		if( j == 0 )
		{
			COM_StripExtension( vname );
			Q_strncat( vname, "_r.mdl", sizeof( vname ));
		}

		for( i = 1; i < MAX_MODELS; i++ )
		{
			if( cl.models[i] && !Q_stricmp( cl.models[i]->name, vname ))
			{
				*index = i;
				return cl.models[i];
			}
		}
	}

	return NULL;
}

enum
{
	DEMOCAM_ANIM_IDLE = 0,
	DEMOCAM_ANIM_DRAW,
	DEMOCAM_ANIM_SHOOT,
	DEMOCAM_ANIM_RELOAD,
};

static const char *democam_anim_names[][4] =
{
	{ "idle", NULL },
	{ "draw", "deploy", NULL },
	{ "shoot", "slash", "throw", "pressbutton" }, // knife, grenades and C4 have no "shoot"
	{ "reload", "insert", NULL }, // shotguns reload shell by shell
};

/*
=================
CL_DemoCamEvent

every event played back goes through here, the weapon ones tell
whether the player who fired had the silencer on
=================
*/
void CL_DemoCamEvent( const char *name, const event_args_t *args )
{
	int i;

	if( !cls.demoplayback || args->entindex < 1 || args->entindex > MAX_CLIENTS )
		return;

	for( i = 0; i < ARRAYSIZE( democam_silencers ); i++ )
	{
		if( !Q_stricmp( name, democam_silencers[i].event ))
			democam_silenced[args->entindex][i] = democam_silencers[i].bparam == 1 ? args->bparam1 : args->bparam2;
	}
}

static qboolean CL_DemoCamSilenced( const cl_entity_t *ent, const model_t *viewmodel )
{
	int i;

	if( ent->index < 1 || ent->index > MAX_CLIENTS )
		return false;

	for( i = 0; i < ARRAYSIZE( democam_silencers ); i++ )
	{
		if( Q_stristr( viewmodel->name, democam_silencers[i].viewmodel ))
			return democam_silenced[ent->index][i];
	}

	return false;
}

static mstudioseqdesc_t *CL_DemoCamSequences( model_t *model, int *numseq )
{
	studiohdr_t *hdr = (studiohdr_t *)Mod_StudioExtradata( model );

	if( !hdr || hdr->numseq <= 0 )
		return NULL;

	*numseq = hdr->numseq;
	return (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex );
}

/*
=================
CL_DemoCamFindSequence

viewmodels have no activities, but their sequences are named the same
way: idle1, shoot2, reload, draw... Weapons with a silencer have a second
set ending in _unsil, used when it's off
=================
*/
static int CL_DemoCamFindSequence( model_t *model, int anim, qboolean silenced )
{
	static int shot;
	mstudioseqdesc_t *seq;
	int numseq, i, j, found[16], numfound = 0;
	qboolean unsil = false;

	if( !( seq = CL_DemoCamSequences( model, &numseq )))
		return -1;

	for( i = 0; i < numseq; i++ )
	{
		if( Q_stristr( seq[i].label, "_unsil" ))
			unsil = true;
	}

	// with the silencer on, the plain names are the ones
	if( silenced )
		unsil = false;

	for( j = 0; j < 4 && democam_anim_names[anim][j] && !numfound; j++ )
	{
		const char *name = democam_anim_names[anim][j];

		for( i = 0; i < numseq && numfound < ARRAYSIZE( found ); i++ )
		{
			const char *label = seq[i].label;

			if( Q_strnicmp( label, name, Q_strlen( name )))
				continue;

			if( unsil != ( Q_stristr( label, "_unsil" ) != NULL ))
				continue;

			// shoot_empty and shootlast are for the last bullet
			if( anim == DEMOCAM_ANIM_SHOOT && ( Q_stristr( label, "empty" ) || Q_stristr( label, "last" )))
				continue;

			found[numfound++] = i;
		}
	}

	if( !numfound )
		return -1;

	// shots take turns between shoot1, shoot2...
	if( anim == DEMOCAM_ANIM_SHOOT )
		return found[shot++ % numfound];

	return found[0];
}

static float CL_DemoCamSequenceLength( model_t *model, int sequence )
{
	mstudioseqdesc_t *seq;
	int numseq;

	if( !( seq = CL_DemoCamSequences( model, &numseq )) || sequence < 0 || sequence >= numseq )
		return 0.0f;

	if( seq[sequence].fps <= 0.0f || seq[sequence].numframes <= 1 )
		return 0.0f;

	return ( seq[sequence].numframes - 1 ) / seq[sequence].fps;
}

/*
=================
CL_DemoCamPlayerAnim

nothing tells what the followed player does with his weapon, but his
own model shows it: CS plays ref_shoot_* / ref_reload_* on the upper
body, and every shot starts the sequence again from frame 0 (animtime
is no use, the server renews it every frame)
=================
*/
static int CL_DemoCamPlayerAnim( const cl_entity_t *ent )
{
	static int last_target, last_sequence;
	static float last_frame;
	mstudioseqdesc_t *seq;
	int numseq, anim = DEMOCAM_ANIM_IDLE;
	qboolean changed;

	// the frame arrives rounded and goes back a little now and then, a new shot drops it a lot
	changed = ent->curstate.sequence != last_sequence || ent->curstate.frame < last_frame - DEMOCAM_FRAME_NOISE;

	// a newly followed player starts with nothing going on
	if( ent->index != last_target )
		changed = false;

	last_target = ent->index;
	last_sequence = ent->curstate.sequence;
	last_frame = ent->curstate.frame;

	if( !changed || !( seq = CL_DemoCamSequences( ent->model, &numseq )) || ent->curstate.sequence >= numseq )
		return DEMOCAM_ANIM_IDLE;

	if( Q_stristr( seq[ent->curstate.sequence].label, "reload" ))
		anim = DEMOCAM_ANIM_RELOAD;
	else if( Q_stristr( seq[ent->curstate.sequence].label, "shoot" ))
		anim = DEMOCAM_ANIM_SHOOT;

	return anim;
}

static void CL_DemoCamSetupViewModel( const cl_entity_t *ent, const vec3_t eyes, const vec3_t angles )
{
	static int last_index, last_target, sequence;
	static qboolean last_silenced;
	static float anim_end;
	cl_entity_t *view = &clgame.viewent;
	int index = 0, anim, newseq = -1, numseq;
	qboolean silenced;

	view->model = CL_DemoCamViewModel( ent, &index );
	view->curstate.modelindex = index;
	anim = CL_DemoCamPlayerAnim( ent );

	if( !view->model )
		return;

	silenced = CL_DemoCamSilenced( ent, view->model );

	if( ent->index != last_target )
	{
		// just switched to him, he isn't drawing the weapon
		last_target = ent->index;
		last_index = index;
		newseq = CL_DemoCamFindSequence( view->model, DEMOCAM_ANIM_IDLE, silenced );
	}
	else if( index != last_index )
	{
		last_index = index;
		newseq = CL_DemoCamFindSequence( view->model, DEMOCAM_ANIM_DRAW, silenced );
	}
	else if( anim != DEMOCAM_ANIM_IDLE )
	{
		newseq = CL_DemoCamFindSequence( view->model, anim, silenced );
	}
	else if( silenced != last_silenced || ( anim_end && cl.time >= anim_end ))
	{
		// the shot or the reload is over, or a shot just told the silencer changed
		newseq = CL_DemoCamFindSequence( view->model, DEMOCAM_ANIM_IDLE, silenced );
		anim_end = 0.0f;
	}

	last_silenced = silenced;

	if( newseq >= 0 )
	{
		float length = CL_DemoCamSequenceLength( view->model, newseq );

		sequence = newseq;
		view->curstate.animtime = cl.time;
		anim_end = length > 0.0f ? cl.time + length : 0.0f;

		// idle loops by itself
		if( !Q_strnicmp( CL_DemoCamSequences( view->model, &numseq )[newseq].label, "idle", 4 ))
			anim_end = 0.0f;
	}

	// a weapon without the animation that was playing
	if( !CL_DemoCamSequences( view->model, &numseq ) || sequence >= numseq )
		sequence = 0;

	view->curstate.sequence = sequence;
	view->curstate.frame = 0.0f;
	view->curstate.framerate = 1.0f;
	view->curstate.body = 0;
	view->curstate.skin = 0;

	VectorCopy( eyes, view->origin );
	VectorCopy( eyes, view->curstate.origin );
	VectorCopy( eyes, view->latched.prevorigin );

	// studio models want the pitch upside down
	VectorSet( view->angles, -angles[PITCH], angles[YAW], 0.0f );
	VectorCopy( view->angles, view->curstate.angles );
	VectorCopy( view->angles, view->latched.prevangles );
}

// the free view starts from whatever was on the screen
static void CL_DemoCamKeepView( const ref_params_t *fd )
{
	VectorCopy( fd->vieworg, democam_last_org );
	VectorCopy( fd->viewangles, democam_last_angles );
}

// true while a key bound to this command is held
static qboolean CL_DemoCamHeld( const char *command )
{
	int key;

	for( key = 0; key < 256; key++ )
	{
		const char *binding;

		if( !Key_IsDown( key ) || !( binding = Key_GetBinding( key )))
			continue;

		if( !Q_stricmp( binding, command ))
			return true;
	}

	return false;
}

/*
=================
CL_DemoCamFreeView

flies around the map like a spectator, through walls. The keys of the
player's movement move it, in real time: also with the demo paused or slowed
=================
*/
static void CL_DemoCamFreeView( ref_params_t *fd )
{
	float speed = DEMOCAM_FREE_SPEED * bound( 0.0, host.realframetime, 0.1 );
	vec3_t forward, right, move;

	if( !democam_free_placed )
	{
		VectorCopy( democam_last_org, democam_free_org );
		VectorCopy( democam_last_angles, democam_free_angles );
		democam_free_angles[ROLL] = 0.0f;
		democam_free_placed = true;
	}

	if( CL_DemoCamHeld( "+speed" ))
		speed *= DEMOCAM_FREE_FAST;

	// the keys only move it while playing, not with the console or a menu up
	VectorClear( move );

	if( cls.key_dest == key_game )
	{
		AngleVectors( democam_free_angles, forward, right, NULL );

		if( CL_DemoCamHeld( "+forward" )) VectorAdd( move, forward, move );
		if( CL_DemoCamHeld( "+back" )) VectorSubtract( move, forward, move );
		if( CL_DemoCamHeld( "+moveright" )) VectorAdd( move, right, move );
		if( CL_DemoCamHeld( "+moveleft" )) VectorSubtract( move, right, move );
		if( CL_DemoCamHeld( "+use" ) || CL_DemoCamHeld( "+moveup" )) move[2] += 1.0f;
		if( CL_DemoCamHeld( "+duck" ) || CL_DemoCamHeld( "+movedown" )) move[2] -= 1.0f;
	}

	if( VectorNormalizeLength( move ) > 0.0f )
		VectorMA( democam_free_org, speed, move, democam_free_org );

	clgame.viewent.model = NULL;
	VectorCopy( democam_free_org, fd->vieworg );
	VectorCopy( democam_free_angles, fd->viewangles );
	CL_DemoCamKeepView( fd );
}

// looking through a sniper scope, that hides the weapon; not the AUG's or SG552's (55)
static qboolean CL_DemoCamScoped( float fov )
{
	return fov > 0.0f && fov <= 40.0f;
}

/*
=================
CL_DemoCamApply

puts the view on the followed player, after client.dll has done its own
=================
*/
void CL_DemoCamApply( ref_params_t *fd )
{
	cl_entity_t *ent;
	vec3_t eyes, angles, forward, end;
	pmtrace_t tr;

	// menu background demos keep their own view
	if( !cls.demoplayback || cl.background )
		return;

	CL_DemoCamDrawInfo();
	CL_DemoCamHudFrame();
	CL_DemoCamFovFrame();

	if( democam_mode == DEMOCAM_FREE )
	{
		CL_DemoCamFreeView( fd );
		return;
	}

	if( !CL_DemoCamActive( ))
	{
		// the recorded view
		if( democam_zoom > 1.0f || CL_DemoCamScoped( democam_demozoom ))
			clgame.viewent.model = NULL;

		CL_DemoCamKeepView( fd );
		return;
	}

	ent = CL_GetEntityByIndex( CL_DemoCamCurrent( ));

	VectorCopy( ent->origin, eyes );
	eyes[2] += ent->curstate.usehull == 1 ? DEMOCAM_DUCK_VIEW_HEIGHT : DEMOCAM_VIEW_HEIGHT;

	// player models carry a third of the real pitch, upside down
	VectorCopy( ent->angles, angles );
	angles[PITCH] *= -3.0f;
	angles[ROLL] = 0.0f;

	if( democam_mode == DEMOCAM_CHASE )
	{
		// starts behind him, looking where he looks
		if( !democam_chase_aimed )
		{
			VectorCopy( ent->index == cl.playernum + 1 ? cl.viewangles : angles, democam_chase_angles );
			democam_chase_angles[ROLL] = 0.0f;
			democam_chase_aimed = true;
		}

		VectorCopy( democam_chase_angles, angles );
		AngleVectors( angles, forward, NULL, NULL );
		VectorMA( eyes, -democam_chase_distance, forward, end );
		end[2] += DEMOCAM_CHASE_HEIGHT;

		// don't go through walls
		tr = CL_TraceLine( eyes, end, PM_STUDIO_IGNORE|PM_GLASS_IGNORE );
		VectorMA( tr.endpos, 4.0f, tr.plane.normal, eyes );
	}

	// no weapon in hand when zoomed in, like a sniper scope
	else if( democam_zoom > 1.0f || CL_DemoCamScoped( CL_DemoCamFollowedFov( )))
		clgame.viewent.model = NULL;
	else CL_DemoCamSetupViewModel( ent, eyes, angles );

	VectorCopy( eyes, fd->vieworg );
	VectorCopy( angles, fd->viewangles );
	CL_DemoCamKeepView( fd );
}
