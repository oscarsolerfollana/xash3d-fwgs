/*
cl_democtl.c - demo playback controls and a radar view from above
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
#include "keydefs.h"
#include "mod_local.h"

/*
==============================================================================
DEMO CONTROLS

While a demo plays back:

  P               pause / resume
  right           faster: x1, x2, x4, x8 (from rewinding, back to x1)
  left            rewind: x1, x2, x4, x8, x16
  up              towards x1: one step up from slow motion, straight to x1
                  from fast forward or rewinding
  down            slower: one step down, to x0.1 (rewinding: rewinds slower)
  pgup / pgdn     a minute forward / back
  home            back to the start
  M               radar: in a corner, full screen, off

A demo can only be read forwards: it stores what changed from one moment
to the next. Every full update in it (the first message, and one every few
seconds in demos recorded by this engine) is remembered as a keyframe:
going back jumps to the keyframe before and reads forward from there as
fast as it can, with the sound off. Rewinding does that every frame.

The radar draws the map from overviews/<map>.txt and .bmp, the same files
HLTV uses, with every player on it.
==============================================================================
*/
#define DEMOCTL_STEP_LONG     60.0f
#define DEMOCTL_END_MARGIN    0.5f  // never seek right to the end, the demo would finish
#define DEMOCTL_SEEK_BATCH    400   // demo messages read per frame while seeking
#define DEMOCTL_MAX_KEYFRAMES 8192
#define DEMOCTL_MAX_REWIND_DT 0.5f  // a slow step doesn't make the next one jump further

// the overview image is drawn for this aspect, like HLTV does
#define OVERVIEW_ASPECT     ( 4.0f / 3.0f )

enum
{
	RADAR_OFF = 0,
	RADAR_CORNER,
	RADAR_FULL,
	RADAR_MODES
};

static const float democtl_speeds[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
#define DEMOCTL_NORMAL_SPEED 3

static const float democtl_rewind_speeds[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
#define DEMOCTL_NOT_REWINDING -1

typedef struct
{
	int   entry;     // demo section
	int   offset;    // where its message starts in the file
	float timestamp; // time inside the section
	float time;      // time from the start of the demo
} democtl_keyframe_t;

static struct
{
	qboolean paused;
	int      speed;
	int      rewind;      // index into democtl_rewind_speeds, or DEMOCTL_NOT_REWINDING
	float    rewind_pos;  // where the rewind is, in demo time
	double   rewind_last; // real time of the last rewind step
	qboolean seeking;
	float    seek_target;
	float    pending_target; // after starting the demo again, < 0 when none
	int      radar;

	char               kf_demo[MAX_QPATH];
	democtl_keyframe_t keyframes[DEMOCTL_MAX_KEYFRAMES];
	int                numkeyframes;

	// overview of the current map
	char     ov_map[64];
	qboolean ov_loaded;
	float    ov_zoom;
	vec3_t   ov_origin;
	qboolean ov_rotated;
	int      ov_texture;
} democtl = { .speed = DEMOCTL_NORMAL_SPEED, .rewind = DEMOCTL_NOT_REWINDING, .pending_target = -1.0f };

static qboolean CL_DemoCtlPlaying( void )
{
	return cls.demoplayback && !cl.background && cls.state == ca_active;
}

static qboolean CL_DemoCtlRewinding( void )
{
	return democtl.rewind != DEMOCTL_NOT_REWINDING;
}

qboolean CL_DemoCtlPaused( void )
{
	return democtl.paused && CL_DemoCtlPlaying() && !democtl.seeking;
}

/*
=================
CL_DemoCtlSeeking

reading ahead as fast as it can, or rewinding, which does that every
frame: either way the sound stays off
=================
*/
qboolean CL_DemoCtlSeeking( void )
{
	return ( democtl.seeking || CL_DemoCtlRewinding( )) && cls.demoplayback;
}

/*
=================
CL_DemoCtlFastForward

the demo has a usercmd for every frame of the client that recorded it and
playback reads one per frame, which caps it at the recorded speed: faster
than x1 or seeking, it has to read as many as the clock asks for
=================
*/
qboolean CL_DemoCtlFastForward( void )
{
	return CL_DemoCtlSeeking() || ( cls.demoplayback && democtl.speed > DEMOCTL_NORMAL_SPEED );
}

/*
=================
CL_DemoCtlRestarting

starting the demo again to go back to another section of it, the camera
keeps following whoever it was following
=================
*/
qboolean CL_DemoCtlRestarting( void )
{
	return democtl.pending_target >= 0.0f;
}

static void CL_DemoCtlSetSpeed( int speed )
{
	democtl.speed = bound( 0, speed, (int)ARRAYSIZE( democtl_speeds ) - 1 );
	Cvar_SetValue( "sys_timescale", democtl_speeds[democtl.speed] );
}

/*
=================
CL_DemoCtlKeyframe

a full update was just read: remember where it is
=================
*/
void CL_DemoCtlKeyframe( void )
{
	democtl_keyframe_t *kf;
	int entry, offset;
	float timestamp;

	// a different demo, its keyframes are not ours
	if( Q_stricmp( democtl.kf_demo, cls.demoname ))
	{
		Q_strncpy( democtl.kf_demo, cls.demoname, sizeof( democtl.kf_demo ));
		democtl.numkeyframes = 0;
	}

	CL_DemoGetPosition( &entry, &offset, &timestamp );

	// read again after going back, it's already there
	if( democtl.numkeyframes > 0 )
	{
		kf = &democtl.keyframes[democtl.numkeyframes - 1];

		if( entry < kf->entry || ( entry == kf->entry && offset <= kf->offset ))
			return;
	}

	if( democtl.numkeyframes >= DEMOCTL_MAX_KEYFRAMES )
		return;

	kf = &democtl.keyframes[democtl.numkeyframes++];
	kf->entry = entry;
	kf->offset = offset;
	kf->timestamp = timestamp;
	kf->time = CL_DemoGetTime();
}

static void CL_DemoCtlStartSeek( float target )
{
	democtl.seeking = true;
	democtl.seek_target = target;
	cls.timedemo = true; // read as fast as possible
}

static void CL_DemoCtlStopSeek( void )
{
	if( !democtl.seeking )
		return;

	democtl.seeking = false;
	cls.timedemo = false;
	CL_DemoResyncClock();
	S_StopAllSounds( false );
}

/*
=================
CL_DemoCtlJumpBack

to the last keyframe before the target, in the section being played
=================
*/
static qboolean CL_DemoCtlJumpBack( float target )
{
	int entry, offset, i;
	float timestamp;

	if( Q_stricmp( democtl.kf_demo, cls.demoname ))
		return false;

	CL_DemoGetPosition( &entry, &offset, &timestamp );

	for( i = democtl.numkeyframes - 1; i >= 0; i-- )
	{
		democtl_keyframe_t *kf = &democtl.keyframes[i];

		if( kf->entry == entry && kf->time <= target )
			return CL_DemoJumpTo( kf->entry, kf->offset, kf->timestamp );
	}

	return false;
}

static void CL_DemoCtlSeek( float target )
{
	float length = CL_DemoGetLength();

	target = bound( 0.0f, target, Q_max( 0.0f, length - DEMOCTL_END_MARGIN ));

	if( target >= CL_DemoGetTime( ))
	{
		CL_DemoCtlStartSeek( target );
		return;
	}

	if( CL_DemoCtlJumpBack( target ))
	{
		CL_DemoCtlStartSeek( target );
		return;
	}

	// another section of the demo: play it again and seek from the start
	democtl.pending_target = target;
	CL_DemoCtlStopSeek();
	Cbuf_AddTextf( "playdemo \"%s.dem\"\n", cls.demoname );
}

static void CL_DemoCtlSeekBy( float delta )
{
	float from;

	if( CL_DemoCtlRewinding( ))
	{
		democtl.rewind_pos = bound( 0.0f, democtl.rewind_pos + delta, CL_DemoGetLength( ));
		return;
	}

	// several presses in a row add up while it's still on its way
	from = democtl.seeking ? democtl.seek_target : CL_DemoGetTime();
	CL_DemoCtlSeek( from + delta );
}

/*
=================
CL_DemoCtlSeekMore

timedemo reads one message per frame; while seeking, keep reading in the
same frame until the target is reached or the batch is done
=================
*/
qboolean CL_DemoCtlSeekMore( void )
{
	static int frame, count;

	if( !democtl.seeking || !cls.demoplayback || CL_DemoGetTime() >= democtl.seek_target )
		return false;

	if( frame != host.framecount )
	{
		frame = host.framecount;
		count = 0;
	}

	return ++count < DEMOCTL_SEEK_BATCH;
}

static void CL_DemoCtlForward( void )
{
	democtl.rewind = DEMOCTL_NOT_REWINDING;
}

static void CL_DemoCtlStartRewind( void )
{
	democtl.rewind = 0;
	democtl.rewind_pos = democtl.seeking ? democtl.seek_target : CL_DemoGetTime();
	democtl.rewind_last = Platform_DoubleTime();
	CL_DemoCtlSetSpeed( DEMOCTL_NORMAL_SPEED );
}

// right: faster, and out of a rewind back to x1
static void CL_DemoCtlFaster( void )
{
	if( CL_DemoCtlRewinding( ))
	{
		CL_DemoCtlForward();
		CL_DemoCtlSetSpeed( DEMOCTL_NORMAL_SPEED );
	}
	else CL_DemoCtlSetSpeed( democtl.speed + 1 );
}

// left: rewind, faster each time
static void CL_DemoCtlRewind( void )
{
	if( !CL_DemoCtlRewinding( ))
		CL_DemoCtlStartRewind();
	else democtl.rewind = Q_min( democtl.rewind + 1, (int)ARRAYSIZE( democtl_rewind_speeds ) - 1 );
}

// up: towards x1
static void CL_DemoCtlTowardsNormal( void )
{
	if( CL_DemoCtlRewinding( ) || democtl.speed > DEMOCTL_NORMAL_SPEED )
	{
		CL_DemoCtlForward();
		CL_DemoCtlSetSpeed( DEMOCTL_NORMAL_SPEED );
	}
	else CL_DemoCtlSetSpeed( democtl.speed + 1 );
}

// down: slower
static void CL_DemoCtlSlower( void )
{
	if( CL_DemoCtlRewinding( ))
		democtl.rewind = Q_max( democtl.rewind - 1, 0 );
	else CL_DemoCtlSetSpeed( democtl.speed - 1 );
}

static void CL_DemoCtlTogglePause( void )
{
	democtl.paused = !democtl.paused;
	democtl.rewind_last = Platform_DoubleTime();
}

/*
=================
CL_DemoCtlKey

takes its keys while a demo plays
=================
*/
qboolean CL_DemoCtlKey( int key, qboolean down )
{
	if( !CL_DemoCtlPlaying() || cls.key_dest != key_game )
		return false;

	switch( key )
	{
	case 'p':
	case K_PAUSE:
	case K_UPARROW:
	case K_DOWNARROW:
	case K_RIGHTARROW:
	case K_LEFTARROW:
	case K_PGUP:
	case K_PGDN:
	case K_HOME:
	case 'm':
		break;
	default:
		return false;
	}

	if( !down )
		return true;

	switch( key )
	{
	case 'p':
	case K_PAUSE:
		CL_DemoCtlTogglePause();
		break;
	case K_RIGHTARROW:
		CL_DemoCtlFaster();
		break;
	case K_LEFTARROW:
		CL_DemoCtlRewind();
		break;
	case K_UPARROW:
		CL_DemoCtlTowardsNormal();
		break;
	case K_DOWNARROW:
		CL_DemoCtlSlower();
		break;
	case K_PGUP:
		CL_DemoCtlSeekBy( DEMOCTL_STEP_LONG );
		break;
	case K_PGDN:
		CL_DemoCtlSeekBy( -DEMOCTL_STEP_LONG );
		break;
	case K_HOME:
		CL_DemoCtlSeekBy( -CL_DemoGetLength( ));
		break;
	case 'm':
		democtl.radar = ( democtl.radar + 1 ) % RADAR_MODES;
		break;
	}

	return true;
}

static void CL_DemoCtlPause_f( void )
{
	if( CL_DemoCtlPlaying( ))
		CL_DemoCtlTogglePause();
}

static void CL_DemoCtlSeek_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "democtl_seek <seconds from the start>\n" );
		return;
	}

	if( CL_DemoCtlPlaying( ))
	{
		CL_DemoCtlForward();
		CL_DemoCtlSeek( Q_atof( Cmd_Argv( 1 )));
	}
}

static void CL_DemoCtlSkip_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "democtl_skip <seconds, negative goes back>\n" );
		return;
	}

	if( CL_DemoCtlPlaying( ))
		CL_DemoCtlSeekBy( Q_atof( Cmd_Argv( 1 )));
}

static void CL_DemoCtlSpeed_f( void )
{
	float wanted = Cmd_Argc() == 2 ? Q_atof( Cmd_Argv( 1 )) : 1.0f;
	int i, best = 0;

	if( !CL_DemoCtlPlaying( ))
		return;

	// negative rewinds
	if( wanted < 0.0f )
	{
		if( !CL_DemoCtlRewinding( ))
			CL_DemoCtlStartRewind();

		for( i = 0; i < ARRAYSIZE( democtl_rewind_speeds ); i++ )
		{
			if( fabs( democtl_rewind_speeds[i] + wanted ) < fabs( democtl_rewind_speeds[best] + wanted ))
				best = i;
		}

		democtl.rewind = best;
		return;
	}

	// the closest of the speeds on offer
	for( i = 0; i < ARRAYSIZE( democtl_speeds ); i++ )
	{
		if( fabs( democtl_speeds[i] - wanted ) < fabs( democtl_speeds[best] - wanted ))
			best = i;
	}

	CL_DemoCtlForward();
	CL_DemoCtlSetSpeed( best );
}

static void CL_DemoCtlRadar_f( void )
{
	if( Cmd_Argc() == 2 )
		democtl.radar = bound( 0, Q_atoi( Cmd_Argv( 1 )), RADAR_MODES - 1 );
	else democtl.radar = ( democtl.radar + 1 ) % RADAR_MODES;
}

void CL_DemoCtlInit( void )
{
	Cmd_AddCommand( "democtl_pause", CL_DemoCtlPause_f, "demo playback: pause or resume" );
	Cmd_AddCommand( "democtl_seek", CL_DemoCtlSeek_f, "demo playback: go to a moment, in seconds from the start" );
	Cmd_AddCommand( "democtl_skip", CL_DemoCtlSkip_f, "demo playback: go forward or back this many seconds" );
	Cmd_AddCommand( "democtl_speed", CL_DemoCtlSpeed_f, "demo playback: speed, 0.1 to 8, or -1 to -16 to rewind" );
	Cmd_AddCommand( "democtl_radar", CL_DemoCtlRadar_f, "demo playback: radar off (0), in a corner (1) or full screen (2)" );
}

/*
=================
CL_DemoCtlFrame

called every frame before the demo is read: finishes a seek once it got
there, takes a rewind one step back, starts the seek left pending by a
restart, and puts everything back when no demo plays
=================
*/
void CL_DemoCtlFrame( void )
{
	if( !cls.demoplayback )
	{
		// started again to go back in time, it keeps the same settings
		if( CL_DemoCtlRestarting( ))
			return;

		// a demo that ended or was stopped leaves no trace
		if( democtl.speed != DEMOCTL_NORMAL_SPEED )
			CL_DemoCtlSetSpeed( DEMOCTL_NORMAL_SPEED );

		democtl.paused = false;
		democtl.seeking = false;
		democtl.rewind = DEMOCTL_NOT_REWINDING;
		democtl.radar = RADAR_OFF;
		return;
	}

	if( !CL_DemoCtlPlaying( ))
		return;

	if( CL_DemoCtlRestarting( ))
	{
		float target = democtl.pending_target;

		democtl.pending_target = -1.0f;

		if( target > 0.0f )
			CL_DemoCtlStartSeek( target );
		return;
	}

	if( democtl.seeking && CL_DemoGetTime() >= democtl.seek_target )
		CL_DemoCtlStopSeek();

	if( CL_DemoCtlRewinding() && !democtl.paused && !democtl.seeking )
	{
		double now = Platform_DoubleTime();
		float dt = bound( 0.0f, now - democtl.rewind_last, DEMOCTL_MAX_REWIND_DT );

		democtl.rewind_last = now;
		democtl.rewind_pos -= democtl_rewind_speeds[democtl.rewind] * dt;

		// at the start it stops there, paused
		if( democtl.rewind_pos <= 0.0f )
		{
			democtl.rewind_pos = 0.0f;
			CL_DemoCtlForward();
			democtl.paused = true;
		}

		CL_DemoCtlSeek( democtl.rewind_pos );
	}
}

/*
==============================================================================
RADAR
==============================================================================
*/

/*
=================
CL_DemoCtlLoadImage

overview images paint what's outside the map pure green, meant to be
keyed out: make it black
=================
*/
static int CL_DemoCtlLoadImage( const char *name )
{
	rgbdata_t *pic = FS_LoadImage( name, NULL, 0 );
	byte *p, *end;
	int stride, tex;

	if( !pic )
		return 0;

	switch( pic->type )
	{
	case PF_INDEXED_24:
	case PF_INDEXED_32:
		p = pic->palette;
		stride = pic->type == PF_INDEXED_24 ? 3 : 4;
		end = p ? p + 256 * stride : NULL;
		break;
	case PF_RGBA_32:
	case PF_RGB_24:
		p = pic->buffer;
		stride = pic->type == PF_RGB_24 ? 3 : 4;
		end = p + (size_t)pic->width * pic->height * stride;
		break;
	default:
		p = end = NULL;
		stride = 4;
		break;
	}

	for( ; p && p < end; p += stride )
	{
		if( p[0] == 0 && p[1] == 255 && p[2] == 0 )
			p[1] = 0;
	}

	tex = ref.dllFuncs.GL_LoadTextureFromBuffer( va( "#democtl_%s", COM_FileWithoutPath( name )), pic, TF_IMAGE|TF_NOMIPMAP, false );
	FS_FreeImage( pic );

	return tex;
}

/*
=================
CL_DemoCtlLoadOverview

reads overviews/<map>.txt; without one, the radar spans the whole level
on a dark background
=================
*/
static void CL_DemoCtlLoadOverview( void )
{
	char path[MAX_QPATH], token[MAX_TOKEN], image[MAX_QPATH];
	char *afile, *pfile;

	if( democtl.ov_loaded && !Q_stricmp( democtl.ov_map, clgame.mapname ))
		return;

	Q_strncpy( democtl.ov_map, clgame.mapname, sizeof( democtl.ov_map ));
	democtl.ov_loaded = true;
	democtl.ov_texture = 0;
	democtl.ov_rotated = world.size[1] <= world.size[0];
	VectorAverage( world.mins, world.maxs, democtl.ov_origin );
	democtl.ov_zoom = 8192.0f / Q_max( 1.0f, world.size[!democtl.ov_rotated] );
	image[0] = 0;

	Q_snprintf( path, sizeof( path ), "overviews/%s.txt", clgame.mapname );

	if( !( afile = (char *)FS_LoadFile( path, NULL, false )))
		return;

	pfile = afile;

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))))
	{
		if( !Q_stricmp( token, "ZOOM" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			democtl.ov_zoom = Q_atof( token );
		}
		else if( !Q_stricmp( token, "ORIGIN" ))
		{
			int i;

			for( i = 0; i < 3; i++ )
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				democtl.ov_origin[i] = Q_atof( token );
			}
		}
		else if( !Q_stricmp( token, "ROTATED" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			democtl.ov_rotated = Q_atoi( token ) != 0;
		}
		else if( !Q_stricmp( token, "IMAGE" ) && !image[0] )
		{
			// only the first layer, CS maps have just one
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			Q_strncpy( image, token, sizeof( image ));
		}
	}

	Mem_Free( afile );

	if( democtl.ov_zoom == 0.0f )
		democtl.ov_zoom = 1.0f;

	if( image[0] )
		democtl.ov_texture = CL_DemoCtlLoadImage( image );
}

/*
=================
CL_DemoCtlWorldToRadar

where a point of the level falls on the overview image, from 0 to 1 on
both axes. Same layout HLTV draws the image with: not rotated, +X goes
up and +Y left; rotated, +X goes right and +Y up
=================
*/
static void CL_DemoCtlWorldToRadar( const vec3_t point, float *u, float *v )
{
	float half_w = 4096.0f / democtl.ov_zoom;
	float half_h = 4096.0f / ( democtl.ov_zoom * OVERVIEW_ASPECT );

	if( democtl.ov_rotated )
	{
		*u = ( point[0] - ( democtl.ov_origin[0] - half_w )) / ( 2.0f * half_w );
		*v = (( democtl.ov_origin[1] + half_h ) - point[1] ) / ( 2.0f * half_h );
	}
	else
	{
		*u = (( democtl.ov_origin[1] + half_w ) - point[1] ) / ( 2.0f * half_w );
		*v = (( democtl.ov_origin[0] + half_h ) - point[0] ) / ( 2.0f * half_h );
	}
}

static void CL_DemoCtlRadarYaw( float yaw, float *du, float *dv )
{
	float rad = DEG2RAD( yaw );
	float dx = cos( rad ), dy = sin( rad );

	if( democtl.ov_rotated )
	{
		*du = dx;
		*dv = -dy;
	}
	else
	{
		*du = -dy;
		*dv = -dx;
	}
}

/*
=================
CL_DemoCtlTeamColor

Counter-Strike tells the teams apart by the player model
=================
*/
static void CL_DemoCtlTeamColor( int index, rgba_t color )
{
	static const char *ct_models[] = { "urban", "gsg9", "sas", "gign", "vip" };
	static const char *t_models[] = { "terror", "leet", "arctic", "guerilla" };
	const char *model = cl.players[index - 1].model;
	int i;

	MakeRGBA( color, 230, 230, 230, 255 );

	for( i = 0; i < ARRAYSIZE( ct_models ); i++ )
	{
		if( !Q_stricmp( model, ct_models[i] ))
			MakeRGBA( color, 90, 160, 255, 255 );
	}

	for( i = 0; i < ARRAYSIZE( t_models ); i++ )
	{
		if( !Q_stricmp( model, t_models[i] ))
			MakeRGBA( color, 255, 90, 60, 255 );
	}
}

static void CL_DemoCtlFill( float x, float y, float w, float h, const rgba_t color )
{
	ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, w, h, color[0], color[1], color[2], color[3] );
}

static void CL_DemoCtlDrawRadar( void )
{
	float x, y, w, h, dot;
	int i, followed = CL_DemoCamTarget();

	CL_DemoCtlLoadOverview();

	if( democtl.radar == RADAR_FULL )
	{
		h = refState.height;
		w = h * OVERVIEW_ASPECT;

		if( w > refState.width )
		{
			w = refState.width;
			h = w / OVERVIEW_ASPECT;
		}

		x = ( refState.width - w ) * 0.5f;
		y = ( refState.height - h ) * 0.5f;
		dot = Q_max( 6.0f, h / 90.0f );

		CL_DemoCtlFill( 0, 0, refState.width, refState.height, (rgba_t){ 0, 0, 0, 255 });
	}
	else
	{
		h = refState.height * 0.33f;
		w = h * OVERVIEW_ASPECT;
		x = 8.0f;
		y = 8.0f;
		dot = Q_max( 4.0f, h / 70.0f );

		CL_DemoCtlFill( x - 2, y - 2, w + 4, h + 4, (rgba_t){ 0, 0, 0, 200 });
	}

	if( democtl.ov_texture )
	{
		ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
		ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
		ref.dllFuncs.R_DrawStretchPic( x, y, w, h, 0, 0, 1, 1, democtl.ov_texture );
	}
	else CL_DemoCtlFill( x, y, w, h, (rgba_t){ 30, 30, 30, 255 });

	for( i = 1; i <= cl.maxclients; i++ )
	{
		cl_entity_t *ent = CL_GetEntityByIndex( i );
		rgba_t color;
		float u, v, du, dv, px, py;
		int k;

		// dead CS players turn into invisible observers
		if( !ent || !ent->model || ent->curstate.messagenum != cl.parsecount || FBitSet( ent->curstate.effects, EF_NODRAW ))
			continue;

		CL_DemoCtlWorldToRadar( ent->origin, &u, &v );

		if( u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f )
			continue;

		px = x + u * w;
		py = y + v * h;
		CL_DemoCtlTeamColor( i, color );

		// where he looks
		CL_DemoCtlRadarYaw( ent->angles[YAW], &du, &dv );
		for( k = 1; k <= 3; k++ )
			CL_DemoCtlFill( px + du * dot * k - dot * 0.2f, py + dv * dot * k - dot * 0.2f, dot * 0.4f, dot * 0.4f, color );

		if( i == followed )
			CL_DemoCtlFill( px - dot, py - dot, dot * 2.0f, dot * 2.0f, (rgba_t){ 255, 220, 0, 255 });

		CL_DemoCtlFill( px - dot * 0.6f, py - dot * 0.6f, dot * 1.2f, dot * 1.2f, color );

		if( democtl.radar == RADAR_FULL && !COM_StringEmpty( cl.players[i - 1].name ))
			Con_DrawString( px + dot, py - dot, cl.players[i - 1].name, color );
	}
}

/*
=================
CL_DemoCtlDraw

the time bar along the bottom and the radar
=================
*/
void CL_DemoCtlDraw( void )
{
	con_nprint_t line = { .time_to_live = 0.1f, .color = { 1.0f, 0.7f, 0.1f }};
	float time, length, frac;
	char speed[16];
	int now, total;

	if( !CL_DemoCtlPlaying( ))
		return;

	if( democtl.radar != RADAR_OFF )
		CL_DemoCtlDrawRadar();

	length = CL_DemoGetLength();

	if( CL_DemoCtlRewinding( ))
		time = democtl.rewind_pos;
	else if( democtl.seeking )
		time = democtl.seek_target;
	else time = CL_DemoGetTime();

	frac = length > 0.0f ? bound( 0.0f, CL_DemoGetTime() / length, 1.0f ) : 0.0f;

	// the bar: what was played, and where a seek is heading
	CL_DemoCtlFill( 0, refState.height - 6, refState.width, 6, (rgba_t){ 0, 0, 0, 160 });
	CL_DemoCtlFill( 0, refState.height - 6, refState.width * frac, 6, (rgba_t){ 255, 180, 30, 220 });

	if( democtl.seeking && !CL_DemoCtlRewinding() && length > 0.0f )
		CL_DemoCtlFill( refState.width * bound( 0.0f, democtl.seek_target / length, 1.0f ) - 1, refState.height - 10, 3, 10, (rgba_t){ 255, 255, 255, 255 });

	now = (int)time;
	total = (int)length;

	if( CL_DemoCtlRewinding( ))
		Q_snprintf( speed, sizeof( speed ), "<< x%d", (int)democtl_rewind_speeds[democtl.rewind] );
	else if( democtl_speeds[democtl.speed] < 1.0f )
		Q_snprintf( speed, sizeof( speed ), "x%.2g", democtl_speeds[democtl.speed] );
	else Q_snprintf( speed, sizeof( speed ), "x%d", (int)democtl_speeds[democtl.speed] );

	line.index = 2;
	Con_NXPrintf( &line, "%s %02d:%02d / %02d:%02d  %s",
		CL_DemoCtlPaused() ? "PAUSA" : democtl.seeking && !CL_DemoCtlRewinding() ? "Avanzando..." : ">",
		now / 60, now % 60, total / 60, total % 60, speed );

	line.index = 3;
	Con_NXPrintf( &line, "P: pausa - der: rapido - izq: rebobinar - arriba: x1 - abajo: lento - RePag/AvPag: 1 min - M: radar" );
}
