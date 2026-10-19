/*
gl_bump.c - relief (normal maps), sun highlights and reflections on the world and brush entities (custom)
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

#include "gl_local.h"
#include "pm_local.h"

/*
A texture with a normal map next to its material,
  materials/<map>.bsp/<texture>_norm.tga  (or materials/common/...)
made by firearms-mapas/desertcrisis/normales.py (relief in RGB, how much each
point shines in alpha), is drawn with a shader instead of the plain texture
(only those in materials/<map>.bsp/reflejos.txt, if the map has one),
on the world and on brush entities (doors, func_wall...):
  - relief (r_bump, off by default): the normal map, turned with the face (its
    texture axes give the directions), lit from the sun (the sky light of the
    map, sv_skyvec) compared with the flat face, so the average brightness stays
  - a highlight of the sun, where the surface is glossy
  - reflections of the map itself, stronger at grazing angles (Fresnel) and,
    on metal, also looking straight at it
This is the base texture pass: the lightmap is multiplied over it afterwards as
always, so all of it is dark in the shadows. Half-Life lightmaps have no light
direction, which is why the sun is used for the relief everywhere.

Probes: on the first frame of a map, a point is put in front of the glossy
surfaces (normal map shining PROBE_GLOSS or more on average), those closer than
PROBE_JOIN share one, and the map is drawn from each in the six directions
(R_DrawCubemapView) into six cells of one big texture (all probes in it). Every
pixel mixes the probes around it, each weighing less the farther it is until
nothing at PROBE_RADIUS: the weights depend only on the place, so the reflection
goes on from one face to the next (choosing two probes per face cut it at the
edges: a lamp seen by the probes of one face and not by those of the next).
Each face gets the PROBES_PER_SURF that reach it.
What a probe sees is divided by its average brightness: the reflection only
shows what is brighter or darker than the rest (lamps, a dark ceiling) and the
light comes from the lightmap as always. With the light of the probe itself, a
floor reflecting a dark ceiling came out darker in patches, one per pair of
probes, and the light was counted twice (the probe and the lightmap).
Around the player (r_reflect_dynamic): every frame a few of the six sides of
the view around the eye are drawn again, small, with the players in them, into a
colour and a depth texture (R_BumpDynamic); gl_post.c reflects them on the
glossy pixels, finding with the depth where the reflection really lands. So it
depends on where the player is, not on where he looks.
A surface with no probe near does not reflect (the sky gradient showed indoors);
with r_reflect_probes 0 all reflect the sky gradient. Probes are taken once:
what moves is not in them.
*/

CVAR_DEFINE_AUTO( r_bump, "1", FCVAR_ARCHIVE, "custom: relief on the textures in materials/maps/<map>.bsp/relieve.txt" );
CVAR_DEFINE_AUTO( r_bump_scale, "1", FCVAR_ARCHIVE, "custom: relief strength" );
CVAR_DEFINE_AUTO( r_specular, "1", FCVAR_ARCHIVE, "custom: sun highlights on glossy surfaces" );
CVAR_DEFINE_AUTO( r_reflect, "1", FCVAR_ARCHIVE, "custom: reflections on glossy surfaces" );
CVAR_DEFINE_AUTO( r_reflect_dynamic, "1", FCVAR_ARCHIVE, "custom: reflections of what is around the player, drawn every frame" );
CVAR_DEFINE_AUTO( r_reflect_dynamic_size, "512", FCVAR_ARCHIVE, "custom: pixels of each side of the reflections around the player (64 to 512: the edges of what they reflect move by one of these pixels, so the bigger the smoother)" );
CVAR_DEFINE_AUTO( r_debug_vista, "", 0, "custom: \"x y z pitch yaw\" puts the view there (for tests: the reflections seen from anywhere)" );
CVAR_DEFINE_AUTO( r_reflect_debug, "0", 0, "custom: shows the planes that compete for the mirrors" );
CVAR_DEFINE_AUTO( r_reflect_cube, "0", FCVAR_ARCHIVE, "custom: the rest of the faces that reflect, from six views around the player (approximate: what the player does not see goes wrong)" );
CVAR_DEFINE_AUTO( r_reflect_bounces, "1", FCVAR_ARCHIVE, "custom: 2 the mirrors show the other mirrors too (the reflection of the reflection); Oscar: off, it did not show and cost frames" );
CVAR_DEFINE_AUTO( r_reflect_cada, "2", FCVAR_ARCHIVE, "custom: each mirror drawn again every that many frames while the view barely changes (1 every frame): they cost the CPU, not the card" );
CVAR_DEFINE_AUTO( r_reflect_medir, "0", 0, "custom: 1 prints every 300 frames how long the frame and the mirrors take (ms)" );
CVAR_DEFINE_AUTO( r_reflect_planes, "4", FCVAR_ARCHIVE, "custom: flat faces (door, floor) reflected as a mirror, drawing the view again (0 to 10)" );
CVAR_DEFINE_AUTO( r_reflect_dynamic_sides, "2", FCVAR_ARCHIVE, "custom: sides of the reflections around the player drawn each frame (1 to 6)" );
CVAR_DEFINE_AUTO( r_reflect_probes, "0", FCVAR_ARCHIVE, "custom: fixed reflections of the map (probes taken when it loads) under the screen ones; Oscar: only the screen ones" );

// the shader is on for any of the three: relief, highlights, reflections
#define R_BumpWanted() ( r_bump.value || r_specular.value > 0.0f || r_reflect.value > 0.0f )

#define MAX_BUMP_TEXTURES	8192
#define BUMP_REFLECT		1
#define BUMP_RELIEF		2
#define BUMP_PLANO		4	// fourth column of reflejos.txt: may be a mirror (r_reflect_planes); without it, always the cube
#define MAX_PROBES		256
#define PROBE_SIZE		64		// pixels of each of the six sides
#define PROBE_JOIN		320.0f		// surfaces closer than this share a probe
#define PROBE_GLOSS		0.03f		// how much a texture has to shine to get one (brillo_desert2.txt: only metal and the floor)
#define PROBE_REACH		1024.0f		// a face with no probe within PROBE_RADIUS uses the nearest up to this
#define PROBE_RADIUS		448.0f		// how far a probe is mixed in
#define PROBES_PER_SURF		32
#define PROBE_COLS		16		// probes per row of the big texture (each 4 x 2 sides)

// the six sides: angles to draw them with; the shader finds them from the direction
static const vec3_t probe_angles[6] =
{
	{ 0, 0, 0 }, { 0, 180, 0 }, { 0, 90, 0 }, { 0, 270, 0 }, { -90, 0, 0 }, { 90, 0, 0 }
};

static struct
{
	qboolean	tried, ok;
	GLhandleARB	prog;
	GLint		u_T, u_B, u_N, u_sun, u_eye, u_hor, u_zen, u_suncol, u_bump, u_spec, u_refl;
	GLint		u_r0, u_r1, u_r2, u_nprobes, u_po, u_pc, u_skyavg, u_ssr, u_rug, u_plano;
	const model_t	*world;
	short		normalmap[MAX_BUMP_TEXTURES];	// -1 not looked for yet, 0 none
	float		gloss[MAX_BUMP_TEXTURES];	// average shine of the normal map
	float		blur[MAX_BUMP_TEXTURES];	// reflejos.txt third column: roughness of its mirror
	float		brillok[MAX_BUMP_TEXTURES];	// command reflejo: shine times this (1), to try values in game
	int		sky0;				// skybox the colours were taken from
	char		*only;				// materials/maps/<map>.bsp/reflejos.txt: the textures that shine and reflect
	char		*relief;			// and relieve.txt: the ones with relief
	char		*ajuste;			// and reflejos_ajuste.txt: what reflejo_guardar wrote, over reflejos.txt
	byte		flags[MAX_BUMP_TEXTURES];	// BUMP_REFLECT, BUMP_RELIEF
	vec3_t		horizon, zenith;

	// probes
	qboolean	probes_done, capturing;
	int		numprobes, too_many;
	vec3_t		probe_org[MAX_PROBES];
	int		probe_tex;			// all of them, PROBE_COLS per row
	int		probe_count[MAX_PROBES];	// surfaces joined, to move the point to their middle
	vec3_t		probe_normal[MAX_PROBES];	// the way the first of them faces
	float		probe_avg[MAX_PROBES];		// average brightness of what it sees
	short		*surf_probe;			// PROBES_PER_SURF per surface of the map: probe + 1, 0 none

	// state while drawing
	qboolean	active, reflects;
	float		curblur;			// of the texture being drawn
	qboolean	curplano;			// of the texture being drawn: BUMP_PLANO
	texture_t	*current;
	const short	*bound_list;			// probes of the last surface, in the uniforms
} bump;

// the view around the player
static struct
{
	// 4 x 2 sides, like the probes; two of them: the one the reflections read (front) and the one being drawn,
	// a few sides a frame, all from the same place. With one, each side was of another place while walking
	// (the last frames of it), read as if of one: the reflections jumped and shook
	GLuint		color[2], depth[2];
	GLuint		fbo, fcolor, fdepth;	// one side is drawn here (copying the depth of the window failed: zero, the reflections hit the surface itself)
	int		size, next, front;
	qboolean	drawing, valid;
	vec3_t		origin[2];	// where all the sides of each were drawn from
	float		zfar[2][6];	// each side its own: the engine sets the far clip from what each one sees
} dyn;

// mirrors: the planes of the faces that reflect, seen in the last frame; the nearest and
// biggest are drawn again from the other side of the plane (the eye reflected on it)
#define MAX_PLANES		10	// gl_post.c MIRRORS too
#define PLANE_CANDIDATES	256	// 32: far away the faces of the crates filled them before the door came
#define PLANE_MIN_AREA		4000.0f		// a stack of crates, the flag poster; 12000 lost one half of the door out of the view, 2000 took the hidden sides between the halves

static struct
{
	vec4_t		cand[PLANE_CANDIDATES];		// normal, distance (in the world)
	float		weight[PLANE_CANDIDATES];	// how much of the view they take
	float		area[PLANE_CANDIDATES];		// of all their faces: small ones (the crates) keep the cube
	float		blur[PLANE_CANDIDATES];		// of their textures
	int		ncand;
	vec4_t		plane[MAX_PLANES];		// drawn this frame
	vec4_t		clip;				// the plane the view being drawn is cut by
	float		zfar[MAX_PLANES];
	vec3_t		ang[MAX_PLANES];		// the angles of each mirror view
	GLuint		fbo2, color2, depth2, copy;	// the second bounce: the view through two mirrors, and a copy of the first
	float		pblur[MAX_PLANES];
	qboolean	valid[MAX_PLANES];		// the image of that place holds its plane from a frame before
	double		since[MAX_PLANES];		// when each mirror got its place (fade in, kept a while)
	float		pfade[MAX_PLANES];
	int		count;
	GLuint		fbo[MAX_PLANES], color[MAX_PLANES], depth[MAX_PLANES];
	int		w, h;
	vec3_t		eye[MAX_PLANES], vf[MAX_PLANES], vr[MAX_PLANES], vu[MAX_PLANES];
	float		tanx, tany;
	qboolean	drawing;
	int		current;
	vec3_t		maineye;			// the PVS of the mirrors: from the real eye
	double		t0, tmir, tframe;		// r_reflect_medir
	int		nframes, nmirrors, nreused, frame;
	vec3_t		pvsfoot;			// and from the foot of the eye on the mirror (what the mirror sees that the eye does not)
} planar;

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER			0x8D40
#define GL_COLOR_ATTACHMENT0		0x8CE0
#define GL_DEPTH_ATTACHMENT		0x8D00
#define GL_FRAMEBUFFER_COMPLETE		0x8CD5
#endif

static void R_BumpPlanes( const ref_viewpass_t *rvp );

static const char *vs_bump =
"uniform vec4 r0, r1, r2;\n"	// object to world (doors, func_wall...)
"varying vec3 wpos;\n"
"void main( void )\n"
"{\n"
"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"	wpos = vec3( dot( r0.xyz, gl_Vertex.xyz ) + r0.w, dot( r1.xyz, gl_Vertex.xyz ) + r1.w, dot( r2.xyz, gl_Vertex.xyz ) + r2.w );\n"
"	gl_FrontColor = gl_Color;\n"
"	gl_Position = ftransform();\n"
"	gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n"	// the mirrors cut what is behind them
"}\n";

#define STR( x ) #x
#define XSTR( x ) STR( x )

static const char *fs_bump =
"uniform sampler2D albedo, normalmap, probe;\n"
"uniform vec3 T, B, N, sun, eye, horizon, zenith, suncol;\n"
"uniform vec4 po[" XSTR( PROBES_PER_SURF ) "];\n"	// probes of the face: where, and their average brightness
"uniform vec2 pc[" XSTR( PROBES_PER_SURF ) "];\n"	// and their place in the big texture
"uniform float bump, spec, refl, nprobes, skyavg, ssr, rug, plano;\n"
"varying vec3 wpos;\n"
"const float S = " XSTR( PROBE_SIZE ) ".0;\n"
"const float RR = " XSTR( PROBE_RADIUS ) " * " XSTR( PROBE_RADIUS ) ";\n"
"const vec2 COLS = vec2( " XSTR( PROBE_COLS ) ".0, " XSTR( MAX_PROBES ) ".0 / " XSTR( PROBE_COLS ) ".0 );\n"
// the cell of the probe texture a direction falls in: same sides as probe_angles,
// right = forward x up (as AngleVectors), copied from the screen with the bottom row first
"vec2 lado( vec3 R )\n"
"{\n"
"	vec3 a = abs( R ), f, u = vec3( 0.0, 0.0, 1.0 );\n"
"	float cell;\n"
"	if( a.x >= a.y && a.x >= a.z ) { f = vec3( sign( R.x ), 0.0, 0.0 ); cell = R.x > 0.0 ? 0.0 : 1.0; }\n"
"	else if( a.y >= a.z ) { f = vec3( 0.0, sign( R.y ), 0.0 ); cell = R.y > 0.0 ? 2.0 : 3.0; }\n"
"	else if( R.z > 0.0 ) { f = vec3( 0.0, 0.0, 1.0 ); u = vec3( -1.0, 0.0, 0.0 ); cell = 4.0; }\n"
"	else { f = vec3( 0.0, 0.0, -1.0 ); u = vec3( 1.0, 0.0, 0.0 ); cell = 5.0; }\n"
"	vec2 c = vec2( dot( R, cross( f, u )), dot( R, u )) / dot( R, f ) * 0.5 + 0.5;\n"
"	vec2 cc = vec2( mod( cell, 4.0 ), floor( cell / 4.0 ));\n"
"	return ( cc + ( 0.5 + clamp( c, 0.0, 1.0 ) * ( S - 1.0 )) / S ) * vec2( 0.25, 0.5 );\n"
"}\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	vec4 a = texture2D( albedo, uv );\n"
"	vec4 nm = texture2D( normalmap, uv );\n"
"	vec3 tn = nm.xyz * 2.0 - 1.0;\n"
"	tn.xy *= bump;\n"
"	vec3 n = normalize( T * tn.x + B * tn.y + N * tn.z );\n"
"	float shade = clamp( 1.0 + ( dot( n, sun ) - dot( N, sun )) * 0.9, 0.45, 1.6 );\n"
"	vec3 V = normalize( eye - wpos );\n"
"	float gloss = nm.a;\n"
"	vec3 H = normalize( V + sun );\n"
"	float s = pow( max( dot( n, H ), 0.0 ), 8.0 + 120.0 * gloss ) * gloss * spec * clamp( dot( N, sun ) * 4.0, 0.0, 1.0 );\n"
"	vec3 R = reflect( -V, n );\n"
"	vec3 env = vec3( 0.0 );\n"
	// metal reflects much more than stone, also looking straight at it
"	float fres = 0.15 + 0.5 * gloss + ( 0.85 - 0.5 * gloss ) * pow( 1.0 - max( dot( n, V ), 0.0 ), 5.0 );\n"
"	float kr = clamp( fres * gloss * 2.0 * refl, 0.0, 1.0 );\n"
"	float k = 0.0;\n"
"	if( nprobes > -0.5 )\n"	// -1: no probe near, no reflection
"	{\n"
"		if( nprobes > 0.5 )\n"
"		{\n"
"			vec2 l = lado( R );\n"
"			float ws = 0.0;\n"
"			for( int i = 0; i < " XSTR( PROBES_PER_SURF ) "; i++ )\n"
"			{\n"
"				if( float( i ) >= nprobes ) break;\n"
"				vec3 d = wpos - po[i].xyz;\n"
"				float w = clamp( 1.0 - dot( d, d ) / RR, 0.0, 1.0 );\n"
"				w = w * w + ( i == 0 ? 0.0001 : 0.0 );\n"	// the nearest, where none reaches
"				if( w > 0.0 ) env += texture2D( probe, ( pc[i] + l ) / COLS ).rgb / po[i].w * w;\n"
"				ws += w;\n"
"			}\n"
"			env /= ws;\n"
"		}\n"
"		else env = mix( horizon, zenith, clamp( R.z, 0.0, 1.0 )) / skyavg;\n"
		// relative to its average (1 = as bright as the rest), lit by the lightmap with the
		// colour of the surface, a bit washed out so dark metal still shows it
"		env = min( env, vec3( 4.0 ));\n"
		// what is darker than the average (the ceiling, for a floor) only darkens glossy
		// surfaces: on concrete each pair of probes saw a different amount of dark ceiling
		// and the floor came out in darker squares
"		env = max( env, mix( vec3( 1.0 ), env, gloss * gloss )) * mix( a.rgb * shade, vec3( dot( a.rgb, vec3( 0.3, 0.59, 0.11 ))), 0.5 );\n"
"		k = kr;\n"
"	}\n"
	// alpha: 1 minus how much it reflects, for the reflections of what is on the screen
	// (gl_post.c); the rest of the view writes 1 there
"	gl_FragColor = vec4( a.rgb * shade * ( 1.0 - k ) + env * k + suncol * s, a.a * ( 1.0 - kr * ssr )) * gl_Color;\n"
	// for gl_post.c: the roughness (0 to 15) as twice the whole part, over 1 minus the strength, and 32 more
	// if the texture may be a mirror (reflejos.txt, fourth column): each pixel knows it, not each plane
"	if( ssr > 0.5 && kr > 0.0 ) gl_FragColor.a += 2.0 * floor( rug * 15.0 + 0.5 ) + 32.0 * plano;\n"
"}\n";

/*
==============
R_BumpReflejo_f

reflejo [texture or start of the name] [shine] [roughness]: the values of the textures that shine,
changed in game to try them (shine from 0, nothing, to 1, a whole mirror looking straight at it: the
strength of the reflection at its average gloss, where the shader saturates; roughness from 0, a sharp
mirror, to 1, the widest blur: RUGOSIDAD_MAX of the third column of reflejos.txt). Without a name, the texture under the crosshair; without values it shows them.
A third value, 1 or 0: whether it may be a mirror (plano) or always the cube (fourth column of reflejos.txt).
Or by words, for the texture under the crosshair and only what they say: reflejo plano, reflejo cubo,
reflejo brillo 0.5, reflejo rugosidad 0.3 (together too: reflejo plano brillo 0.8).
reflejos [start of the name]: only shows them; alone, all of them.
reflejos_calidad alta|baja: mirrors for the marked textures and the cube for the rest, or the cube for all.
reflejo_guardar writes them to materials/maps/<map>.bsp/reflejos.txt
==============
*/
static int R_BumpNormalMap( texture_t *t, int index );
static qboolean R_BumpSkipTexture( const texture_t *t );

static vec3_t	mirada_org, mirada_ang;	// the view of the player, for reflejo (R_BumpPlanes)

// the reflection of texture i looking straight at it with shine times 1, as the shader (fres * gloss * 2
// at its average gloss): the shine of reflejo goes from 0 to 1 over it, 1 where it saturates
static float R_BumpReflejoBase( int i )
{
	float g = bump.gloss[i];
	return Q_max(( 0.15f + 0.5f * g ) * g * 2.0f, 0.01f );
}

#define RUGOSIDAD_MAX	0.3f	// the third column of reflejos.txt that blurs the lamps of a ceiling on the floor as much as the shader does (40 pixels)

// the values of texture i, changed with the arguments from arg on
// a word of reflejo for the texture under the crosshair: plano, cubo, brillo X, rugosidad X
static qboolean R_BumpReflejoPalabra( const char *a )
{
	return !Q_stricmp( a, "plano" ) || !Q_stricmp( a, "cubo" ) || !Q_stricmp( a, "brillo" ) || !Q_stricmp( a, "rugosidad" );
}

static void R_BumpReflejoValores( texture_t *t, int i, int arg )
{
	float base = R_BumpReflejoBase( i );

	if( gEngfuncs.Cmd_Argc() > arg && R_BumpReflejoPalabra( gEngfuncs.Cmd_Argv( arg )))
	{
		// by words, only what they say: reflejo plano, reflejo brillo 0.5 rugosidad 0.3
		for( int k = arg; k < gEngfuncs.Cmd_Argc(); k++ )
		{
			const char *a = gEngfuncs.Cmd_Argv( k );

			if( !Q_stricmp( a, "plano" )) SetBits( bump.flags[i], BUMP_PLANO );
			else if( !Q_stricmp( a, "cubo" )) ClearBits( bump.flags[i], BUMP_PLANO );
			else if( !Q_stricmp( a, "brillo" ) && k + 1 < gEngfuncs.Cmd_Argc())
				{ k++; bump.brillok[i] = bound( 0.0f, Q_atof( gEngfuncs.Cmd_Argv( k )), 1.0f ) / base; }	// k apart: Cmd_Argv is a macro, ++k in it went two on
			else if( !Q_stricmp( a, "rugosidad" ) && k + 1 < gEngfuncs.Cmd_Argc())
				{ k++; bump.blur[i] = bound( 0.0f, Q_atof( gEngfuncs.Cmd_Argv( k )), 1.0f ) * RUGOSIDAD_MAX; }
		}
		arg = 99;
	}
	if( gEngfuncs.Cmd_Argc() > arg ) bump.brillok[i] = bound( 0.0f, Q_atof( gEngfuncs.Cmd_Argv( arg )), 1.0f ) / base;
	if( gEngfuncs.Cmd_Argc() > arg + 1 ) bump.blur[i] = bound( 0.0f, Q_atof( gEngfuncs.Cmd_Argv( arg + 1 )), 1.0f ) * RUGOSIDAD_MAX;
	if( gEngfuncs.Cmd_Argc() > arg + 2 )
	{
		if( Q_atoi( gEngfuncs.Cmd_Argv( arg + 2 ))) SetBits( bump.flags[i], BUMP_PLANO );
		else ClearBits( bump.flags[i], BUMP_PLANO );
	}
	gEngfuncs.Con_Printf( "%-16s brillo %.2f  rugosidad %.2f  %s\n", t->name, Q_min( base * bump.brillok[i], 1.0f ), Q_min( bump.blur[i] / RUGOSIDAD_MAX, 1.0f ),
		FBitSet( bump.flags[i], BUMP_PLANO ) ? "plano" : "cubo" );
}

// the textures whose name starts with the first argument; cambiar: with the values after it
static void R_BumpReflejoNombre( const char *cmd, qboolean cambiar )
{
	const char *pref = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv( 1 ) : "";
	int i, n = 0;

	if( !WORLDMODEL )
		return;
	for( i = 0; i < WORLDMODEL->numtextures && i < MAX_BUMP_TEXTURES; i++ )
	{
		texture_t *t = WORLDMODEL->textures[i];

		if( !t || R_BumpSkipTexture( t ) || Q_strnicmp( t->name, pref, Q_strlen( pref )))
			continue;
		R_BumpNormalMap( t, i );
		if( !FBitSet( bump.flags[i], BUMP_REFLECT ))
			continue;
		R_BumpReflejoValores( t, i, cambiar ? 2 : 99 );
		n++;
	}
	if( !n )
		gEngfuncs.Con_Printf( "%s: no texture that reflects starts with \"%s\" (the ones in reflejos.txt)\n", cmd, pref );
}

static void R_BumpReflejos_f( void )
{
	R_BumpReflejoNombre( "reflejos", false );
}

static void R_BumpReflejo_f( void )
{
	vec3_t fwd, end;
	pmtrace_t traza;
	msurface_t *surf;
	texture_t *t;
	int i;

	if( !WORLDMODEL )
		return;
	if( gEngfuncs.Cmd_Argc() > 1 )
	{
		const char *a = gEngfuncs.Cmd_Argv( 1 );

		if( !(( a[0] >= '0' && a[0] <= '9' ) || a[0] == '.' ) && !R_BumpReflejoPalabra( a ))
		{
			R_BumpReflejoNombre( "reflejo", true );	// a name: those textures
			return;
		}
	}
	AngleVectors( mirada_ang, fwd, NULL, NULL );
	VectorMA( mirada_org, 8192.0f, fwd, end );
	traza = gEngfuncs.CL_TraceLine( mirada_org, end, PM_STUDIO_IGNORE );
	surf = gEngfuncs.EV_TraceSurface( traza.ent > 0 ? traza.ent : 0, mirada_org, end );
	if( !surf || !surf->texinfo || !( t = surf->texinfo->texture ))
	{
		gEngfuncs.Con_Printf( "reflejo: no texture under the crosshair\n" );
		return;
	}
	for( i = 0; i < WORLDMODEL->numtextures && i < MAX_BUMP_TEXTURES; i++ )
		if( WORLDMODEL->textures[i] == t )
			break;
	if( i >= WORLDMODEL->numtextures || i >= MAX_BUMP_TEXTURES || R_BumpSkipTexture( t ))
	{
		gEngfuncs.Con_Printf( "%-16s (not a texture of the map)\n", t->name );
		return;
	}
	R_BumpNormalMap( t, i );
	if( !FBitSet( bump.flags[i], BUMP_REFLECT ))
	{
		gEngfuncs.Con_Printf( "%-16s does not reflect (not in reflejos.txt)\n", t->name );
		return;
	}
	R_BumpReflejoValores( t, i, 1 );
}

// alta: the marked textures as mirrors, the rest with the cube; baja: all with the cube
static void R_BumpReflejosCalidad_f( void )
{
	const char *q = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv( 1 ) : "";

	if( !Q_stricmp( q, "alta" ))
	{
		gEngfuncs.Cvar_Set( "r_reflect_planes", r_reflect_planes.value > 0.0f ? r_reflect_planes.string : "4" );
	}
	else if( !Q_stricmp( q, "baja" ))
	{
		gEngfuncs.Cvar_Set( "r_reflect_planes", "0" );
	}
	else
	{
		gEngfuncs.Con_Printf( "reflejos_calidad alta|baja (now %s: %d mirrors)\n", r_reflect_planes.value > 0.0f ? "alta" : "baja", (int)r_reflect_planes.value );
		return;
	}
	gEngfuncs.Cvar_Set( "r_reflect", "1" );
	gEngfuncs.Cvar_Set( "r_reflect_dynamic", "1" );
	gEngfuncs.Cvar_Set( "r_reflect_cube", "1" );
	gEngfuncs.Cvar_Set( "r_ssr", "0" );
	gEngfuncs.Con_Printf( "reflejos_calidad %s: %d mirrors, the cube for the rest\n", q, (int)r_reflect_planes.value );
}

static int R_BumpNormalMap( texture_t *t, int index );

// a number as short as it reads: 0.21, 1, 0.5
static void R_BumpNumero( char *buf, size_t size, float v )
{
	char *e;

	Q_snprintf( buf, size, "%.3f", v );
	e = buf + Q_strlen( buf ) - 1;
	while( e > buf && *e == '0' ) *e-- = 0;
	if( *e == '.' ) *e = 0;
}

/*
reflejo_guardar: the values of reflejo into reflejos.txt itself, its comments and order kept. The line of a
texture (the first whose name starts its name, as R_BumpListed reads it) takes them: in its columns if
it is the line of that texture alone, or a line of its own just before it if it is of several (ammo0).
Columns: brillo for normales.py (baked into the _norm, not read here: kept), roughness, plano, and a
fourth, the shine of reflejo (times the baked one), only when it is not 1.
*/
static void R_BumpReflejoGuardar_f( void )
{
	char	path[256], mapa[64], *out;
	byte	hecha[MAX_BUMP_TEXTURES];
	const char *p;
	size_t	cap, n = 0;
	int	cambios = 0;
	file_t	*f;

	if( !WORLDMODEL )
		return;
	if( !bump.only )
	{
		gEngfuncs.Con_Printf( "reflejo_guardar: this map has no reflejos.txt\n" );
		return;
	}
	COM_FileBase( WORLDMODEL->name, mapa, sizeof( mapa ));
	memset( hecha, 0, sizeof( hecha ));
	for( int i = 0; i < WORLDMODEL->numtextures && i < MAX_BUMP_TEXTURES; i++ )
		if( WORLDMODEL->textures[i] && bump.normalmap[i] < 0 )
			R_BumpNormalMap( WORLDMODEL->textures[i], i );	// its values read, though not seen yet
	cap = Q_strlen( bump.only ) + WORLDMODEL->numtextures * 96 + 256;
	out = Mem_Malloc( r_temppool, cap );

	for( p = bump.only; *p; )
	{
		const char *ls = p, *le = p, *w, *q;
		float c[5] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
		char c1[32] = "1", linea[256];
		int wl, nc = 0, propia = -1;

		while( *le && *le != '\n' ) le++;
		p = *le ? le + 1 : le;

		for( w = ls; w < le && ( *w == ' ' || *w == '\t' ); w++ );
		for( q = w; q < le && *q != ' ' && *q != '\t' && *q != '\r'; q++ );
		wl = q - w;
		linea[0] = 0;
		if( wl > 0 && *w != '#' )
		{
			// its columns
			while( nc < 4 )
			{
				char *end;
				float v;

				while( q < le && ( *q == ' ' || *q == '\t' )) q++;
				if( q >= le || *q == '\r' ) break;
				v = strtod( q, &end );
				if( end == q ) break;
				if( nc == 0 ) Q_strncpy( c1, q, Q_min( (int)sizeof( c1 ), (int)( end - q ) + 1 ));
				c[++nc] = v;
				q = end;
			}
			for( int i = 0; i < WORLDMODEL->numtextures && i < MAX_BUMP_TEXTURES; i++ )
			{
				texture_t *t = WORLDMODEL->textures[i];
				float rug, pl, br;
				char a[16], b[16];

				if( !t || hecha[i] || Q_strnicmp( t->name, w, wl ))
					continue;
				hecha[i] = 1;
				if( bump.normalmap[i] < 0 || !FBitSet( bump.flags[i], BUMP_REFLECT ))
					continue;	// not drawn yet, or not reflecting: as it is
				rug = c[2] >= 0.0f ? c[2] : 0.0f;
				pl = c[3] >= 0.0f ? c[3] : 0.0f;
				br = c[4] >= 0.0f ? c[4] : 1.0f;
				if( fabs( rug - bump.blur[i] ) < 0.0005f && ( pl > 0.5f ) == ( FBitSet( bump.flags[i], BUMP_PLANO ) != 0 ) && fabs( br - bump.brillok[i] ) < 0.005f )
					continue;	// already so
				if( Q_strlen( t->name ) == wl )
				{
					propia = i;	// its own line: its columns
					continue;
				}
				// a line of its own before the one of several
				R_BumpNumero( a, sizeof( a ), bump.blur[i] );
				R_BumpNumero( b, sizeof( b ), bump.brillok[i] );
				if( fabs( bump.brillok[i] - 1.0f ) < 0.005f ) b[0] = 0;
				n += Q_snprintf( out + n, cap - n, "%s %s %s %d%s%s\n", t->name, c1, a, FBitSet( bump.flags[i], BUMP_PLANO ) ? 1 : 0, b[0] ? " " : "", b );
				cambios++;
			}
			if( propia >= 0 )
			{
				char a[16], b[16], nombre[64];

				Q_strncpy( nombre, w, Q_min( (int)sizeof( nombre ), wl + 1 ));
				R_BumpNumero( a, sizeof( a ), bump.blur[propia] );
				R_BumpNumero( b, sizeof( b ), bump.brillok[propia] );
				if( fabs( bump.brillok[propia] - 1.0f ) < 0.005f ) b[0] = 0;
				Q_snprintf( linea, sizeof( linea ), "%s %s %s %d%s%s\n", nombre, c1, a, FBitSet( bump.flags[propia], BUMP_PLANO ) ? 1 : 0, b[0] ? " " : "", b );
				cambios++;
			}
		}
		if( linea[0] )
			n += Q_snprintf( out + n, cap - n, "%s", linea );
		else
		{
			size_t l = Q_min( (size_t)( le - ls ), cap - n - 2 );
			while( l > 0 && ls[l-1] == '\r' ) l--;
			memcpy( out + n, ls, l );
			n += l;
			if( *le ) out[n++] = '\n';
		}
		out[n] = 0;
	}

	if( !cambios )
	{
		gEngfuncs.Con_Printf( "reflejo_guardar: reflejos.txt already has them\n" );
		Mem_Free( out );
		return;
	}
	// the one before, once
	Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/reflejos.txt.antes", mapa );
	if( !gEngfuncs.fsapi->FileExists( path, true ) && ( f = gEngfuncs.fsapi->Open( path, "w", true )))
	{
		gEngfuncs.fsapi->Write( f, bump.only, Q_strlen( bump.only ));
		gEngfuncs.fsapi->Close( f );
	}
	Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/reflejos.txt", mapa );
	if( !( f = gEngfuncs.fsapi->Open( path, "w", true )))
	{
		gEngfuncs.Con_Printf( S_ERROR "reflejo_guardar: can't write %s\n", path );
		Mem_Free( out );
		return;
	}
	gEngfuncs.fsapi->Write( f, out, n );
	gEngfuncs.fsapi->Close( f );
	Mem_Free( bump.only );
	bump.only = out;
	// what reflejo_guardar wrote before apart: now in reflejos.txt
	if( bump.ajuste )
	{
		Mem_Free( bump.ajuste );
		bump.ajuste = NULL;
		Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/reflejos_ajuste.txt", mapa );
		gEngfuncs.fsapi->Delete( path );
	}
	gEngfuncs.Con_Printf( "reflejo_guardar: %d changes in materials/maps/%s.bsp/reflejos.txt\n", cambios, mapa );
}

void R_InitBump( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_bump );
	gEngfuncs.Cvar_RegisterVariable( &r_bump_scale );
	gEngfuncs.Cvar_RegisterVariable( &r_specular );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_probes );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_dynamic );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_dynamic_size );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_dynamic_sides );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_planes );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_medir );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_cada );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_bounces );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_cube );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_debug );
	gEngfuncs.Cvar_RegisterVariable( &r_debug_vista );
	gEngfuncs.Cmd_AddCommand( "reflejos", R_BumpReflejos_f, "custom: reflejos [start of the name]: the values of the textures that reflect" );
	gEngfuncs.Cmd_AddCommand( "reflejo", R_BumpReflejo_f, "custom: reflejo [texture] [shine] [roughness]: changes them (without a name, the texture under the crosshair)" );
	gEngfuncs.Cmd_AddCommand( "reflejos_calidad", R_BumpReflejosCalidad_f, "custom: alta, mirrors for the textures marked plano in reflejos.txt and the cube for the rest; baja, the cube for all" );
	gEngfuncs.Cmd_AddCommand( "reflejo_guardar", R_BumpReflejoGuardar_f, "custom: writes the values of reflejo to materials/maps/<map>.bsp/reflejos.txt" );
	memset( &bump, 0, sizeof( bump ));
}

static qboolean R_BumpProgram( void )
{
	GLhandleARB v, f;
	GLint ok = 0;
	char log[2048];

	if( bump.tried )
		return bump.ok;
	bump.tried = true;

	if( !GL_Support( GL_SHADER_GLSL100_EXT ) || !GL_Support( GL_SHADER_OBJECTS_EXT ) || gEngfuncs.Sys_CheckParm( "-gl2shim" ))
		return false;
	if( glConfig.max_texture_units < 5 )
		return false;

	v = pglCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	pglShaderSourceARB( v, 1, &vs_bump, NULL );
	pglCompileShaderARB( v );
	pglGetObjectParameterivARB( v, GL_OBJECT_COMPILE_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( v, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_bump: vertex shader: %s\n", log );
		return false;
	}
	f = pglCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	pglShaderSourceARB( f, 1, &fs_bump, NULL );
	pglCompileShaderARB( f );
	pglGetObjectParameterivARB( f, GL_OBJECT_COMPILE_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( f, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_bump: shader: %s\n", log );
		return false;
	}
	bump.prog = pglCreateProgramObjectARB();
	pglAttachObjectARB( bump.prog, v );
	pglAttachObjectARB( bump.prog, f );
	pglLinkProgramARB( bump.prog );
	pglGetObjectParameterivARB( bump.prog, GL_OBJECT_LINK_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( bump.prog, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_bump: program: %s\n", log );
		return false;
	}

	pglUseProgramObjectARB( bump.prog );
	pglUniform1iARB( pglGetUniformLocationARB( bump.prog, "albedo" ), 0 );
	pglUniform1iARB( pglGetUniformLocationARB( bump.prog, "normalmap" ), 2 );
	pglUniform1iARB( pglGetUniformLocationARB( bump.prog, "probe" ), 3 );
	bump.u_T = pglGetUniformLocationARB( bump.prog, "T" );
	bump.u_B = pglGetUniformLocationARB( bump.prog, "B" );
	bump.u_N = pglGetUniformLocationARB( bump.prog, "N" );
	bump.u_sun = pglGetUniformLocationARB( bump.prog, "sun" );
	bump.u_eye = pglGetUniformLocationARB( bump.prog, "eye" );
	bump.u_hor = pglGetUniformLocationARB( bump.prog, "horizon" );
	bump.u_zen = pglGetUniformLocationARB( bump.prog, "zenith" );
	bump.u_suncol = pglGetUniformLocationARB( bump.prog, "suncol" );
	bump.u_bump = pglGetUniformLocationARB( bump.prog, "bump" );
	bump.u_spec = pglGetUniformLocationARB( bump.prog, "spec" );
	bump.u_refl = pglGetUniformLocationARB( bump.prog, "refl" );
	bump.u_r0 = pglGetUniformLocationARB( bump.prog, "r0" );
	bump.u_r1 = pglGetUniformLocationARB( bump.prog, "r1" );
	bump.u_r2 = pglGetUniformLocationARB( bump.prog, "r2" );
	bump.u_nprobes = pglGetUniformLocationARB( bump.prog, "nprobes" );
	bump.u_po = pglGetUniformLocationARB( bump.prog, "po" );
	bump.u_pc = pglGetUniformLocationARB( bump.prog, "pc" );
	bump.u_skyavg = pglGetUniformLocationARB( bump.prog, "skyavg" );
	bump.u_ssr = pglGetUniformLocationARB( bump.prog, "ssr" );
	bump.u_rug = pglGetUniformLocationARB( bump.prog, "rug" );
	bump.u_plano = pglGetUniformLocationARB( bump.prog, "plano" );
	pglUseProgramObjectARB( 0 );
	bump.ok = true;
	return true;
}

// average of a texture read back from a small mip level, in 0..1
static void R_BumpAverage( int texnum, vec4_t out, qboolean top_half )
{
	const gl_texture_t *t;
	byte *px;
	int w, h, x, y, level = 0, n = 0;
	double c[4] = { 0, 0, 0, 0 };

	Vector4Set( out, 0.5f, 0.5f, 0.5f, 0.0f );
	if( texnum <= 0 )
		return;
	t = R_GetTexture( texnum );
	w = t->width; h = t->height;
	if( w <= 0 || h <= 0 )
		return;
	if( !FBitSet( t->flags, TF_NOMIPMAP ))
	{
		while(( w > 64 || h > 64 ) && level + 1 < t->numMips )
		{
			w = Q_max( 1, w >> 1 );
			h = Q_max( 1, h >> 1 );
			level++;
		}
	}
	if( w > 4096 || h > 4096 )
		return;
	px = Mem_Malloc( r_temppool, w * h * 4 );
	GL_Bind( XASH_TEXTURE0, texnum );
	pglGetTexImage( GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, px );
	for( y = 0; y < ( top_half ? h / 2 : h ); y++ )
	{
		for( x = 0; x < w; x++ )
		{
			const byte *p = px + ( y * w + x ) * 4;
			c[0] += p[0]; c[1] += p[1]; c[2] += p[2]; c[3] += p[3];
			n++;
		}
	}
	Mem_Free( px );
	if( n )
		Vector4Set( out, c[0] / n / 255.0, c[1] / n / 255.0, c[2] / n / 255.0, c[3] / n / 255.0 );
}

static void R_BumpSky( void )
{
	vec4_t c;
	int i, n = 0;

	if( tr.skyboxTextures[0] == bump.sky0 && bump.sky0 )
		return;
	bump.sky0 = tr.skyboxTextures[0];

	// zenith: the top side; horizon: the upper half of the four around
	R_BumpAverage( tr.skyboxTextures[4], c, false );
	VectorCopy( c, bump.zenith );
	VectorClear( bump.horizon );
	for( i = 0; i < 4; i++ )
	{
		if( !tr.skyboxTextures[i] )
			continue;
		R_BumpAverage( tr.skyboxTextures[i], c, true );
		VectorAdd( bump.horizon, c, bump.horizon );
		n++;
	}
	if( n ) VectorScale( bump.horizon, 1.0f / n, bump.horizon );
	else VectorSet( bump.horizon, 0.5f, 0.5f, 0.5f );
}

static void R_BumpFreeProbes( void )
{
	if( bump.probe_tex )
		GL_FreeTexture( bump.probe_tex );
	bump.probe_tex = 0;
	bump.numprobes = bump.too_many = 0;
	bump.probes_done = false;
	if( bump.surf_probe )
		Mem_Free( bump.surf_probe );
	bump.surf_probe = NULL;
}

// a new map: forget what was found for the last one
static void R_BumpCheckWorld( void )
{
	if( bump.world == WORLDMODEL )
		return;
	bump.world = WORLDMODEL;
	memset( bump.normalmap, 0xff, sizeof( bump.normalmap ));	// -1: not looked for
	if( bump.only )
		Mem_Free( bump.only );
	if( bump.relief )
		Mem_Free( bump.relief );
	if( bump.ajuste )
		Mem_Free( bump.ajuste );
	bump.only = bump.relief = bump.ajuste = NULL;
	if( WORLDMODEL )
	{
		char path[256], mapa[64];

		COM_FileBase( WORLDMODEL->name, mapa, sizeof( mapa ));
		Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/reflejos.txt", mapa );
		bump.only = (char *)gEngfuncs.fsapi->LoadFile( path, NULL, false );
		Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/relieve.txt", mapa );
		bump.relief = (char *)gEngfuncs.fsapi->LoadFile( path, NULL, false );
		Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/reflejos_ajuste.txt", mapa );
		bump.ajuste = (char *)gEngfuncs.fsapi->LoadFile( path, NULL, false );
	}
	bump.sky0 = 0;
	R_BumpFreeProbes();
}

// a texture in a list: "name" or the start of the name, one per line, # comments;
// col: the value in that column of its line (1 the first after the name), or -1
static qboolean R_BumpListed( const char *p, const char *name, int col, float *value )
{
	if( value )
		*value = -1.0f;
	while( *p )
	{
		char word[64];
		int n = 0;

		while( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' )
			p++;
		if( *p == '#' )
		{
			while( *p && *p != '\n' )
				p++;
			continue;
		}
		while( *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' )
		{
			if( n < (int)sizeof( word ) - 1 )
				word[n++] = *p;
			p++;
		}
		word[n] = 0;
		if( n && !Q_strnicmp( name, word, n ))
		{
			int c;

			for( c = 1; value && c <= col; c++ )
			{
				char *end;
				float v;

				while( *p == ' ' || *p == '\t' )
					p++;
				v = strtod( p, &end );
				if( end == p )
					break;
				p = end;
				if( c == col )
					*value = v;
			}
			return true;
		}
		while( *p && *p != '\n' )	// the rest of the line (a value, for normales.py)
			p++;
	}
	return false;
}

static int R_BumpNormalMap( texture_t *t, int index )
{
	char path[256], mapa[64];
	int texnum = 0;

	if( index < 0 || index >= MAX_BUMP_TEXTURES )
		return 0;
	R_BumpCheckWorld();
	if( bump.normalmap[index] >= 0 )
		return bump.normalmap[index];

	// reflejos.txt: only the textures in it shine and reflect (all, without it);
	// relieve.txt: only those have relief (none, without it); the rest are drawn as always
	bump.flags[index] = 0;
	bump.blur[index] = 0.0f;
	if( !bump.only || R_BumpListed( bump.only, t->name, 2, &bump.blur[index] ))
		bump.flags[index] |= BUMP_REFLECT;
	bump.blur[index] = Q_max( bump.blur[index], 0.0f );
	{
		float plano = 1.0f;	// without reflejos.txt all may be mirrors, as before

		if( bump.only && !R_BumpListed( bump.only, t->name, 3, &plano ))
			plano = 0.0f;
		if( plano > 0.5f && FBitSet( bump.flags[index], BUMP_REFLECT ))
			bump.flags[index] |= BUMP_PLANO;
	}
	bump.brillok[index] = 1.0f;
	if( bump.only )
	{
		float v;
		if( R_BumpListed( bump.only, t->name, 4, &v ) && v >= 0.0f )
			bump.brillok[index] = v;	// fourth column: the shine of reflejo, saved by reflejo_guardar
	}
	// what was tried in game and saved (reflejo_guardar): its shine, roughness and plano, over reflejos.txt;
	// without reading it, a texture turned to plano was the cube again after a retry
	if( bump.ajuste && R_BumpListed( bump.ajuste, t->name, 0, NULL ))
	{
		float v;

		bump.flags[index] |= BUMP_REFLECT;
		if( R_BumpListed( bump.ajuste, t->name, 1, &v ) && v >= 0.0f ) bump.brillok[index] = v;
		if( R_BumpListed( bump.ajuste, t->name, 2, &v ) && v >= 0.0f ) bump.blur[index] = v;
		if( R_BumpListed( bump.ajuste, t->name, 3, &v ) && v >= 0.0f )
		{
			if( v > 0.5f ) bump.flags[index] |= BUMP_PLANO;
			else bump.flags[index] &= ~BUMP_PLANO;
		}
	}
	if( bump.relief && R_BumpListed( bump.relief, t->name, 0, NULL ))
		bump.flags[index] |= BUMP_RELIEF;
	bump.normalmap[index] = 0;
	bump.gloss[index] = 0.0f;
	if( !bump.flags[index] )
		return 0;

	COM_FileBase( WORLDMODEL->name, mapa, sizeof( mapa ));
	Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/%s_norm.tga", mapa, t->name );
	if( !gEngfuncs.fsapi->FileExists( path, false ))
		Q_snprintf( path, sizeof( path ), "materials/common/%s_norm.tga", t->name );
	if( gEngfuncs.fsapi->FileExists( path, false ))
		texnum = GL_LoadTexture( path, NULL, 0, TF_NORMALMAP );
	bump.normalmap[index] = texnum > 0 ? texnum : 0;
	bump.gloss[index] = 0.0f;
	if( texnum > 0 && FBitSet( bump.flags[index], BUMP_REFLECT ))
	{
		vec4_t c;
		R_BumpAverage( texnum, c, false );
		bump.gloss[index] = c[3];
	}
	gEngfuncs.Con_DPrintf( "bump: %s flags %d normal map %d gloss %.2f\n", t->name, bump.flags[index], texnum, bump.gloss[index] );
	return bump.normalmap[index];
}

static int R_BumpTextureIndex( const texture_t *t )
{
	int i;

	for( i = 0; i < WORLDMODEL->numtextures && i < MAX_BUMP_TEXTURES; i++ )
	{
		if( WORLDMODEL->textures[i] == t )
			return i;
	}
	return -1;
}

static qboolean R_BumpSkipTexture( const texture_t *t )
{
	return !t || t->name[0] == '{' || t->name[0] == '!' || t->name[0] == '*' || !Q_strnicmp( t->name, "sky", 3 );
}

static qboolean R_BumpInsideWall( const vec3_t p )
{
	mleaf_t *leaf = gEngfuncs.Mod_PointInLeaf( p, WORLDMODEL->nodes, WORLDMODEL );
	return !leaf || leaf->contents == CONTENTS_SOLID;
}

// a glossy surface: where its probe would go (in front of it, not inside a wall)
static qboolean R_BumpProbePoint( const msurface_t *s, vec3_t p, vec3_t c, vec3_t n )
{
	texture_t *t = s->texinfo ? s->texinfo->texture : NULL;
	int k, ti;

	if( !t || R_BumpSkipTexture( t ) || !s->polys || FBitSet( s->flags, SURF_DRAWSKY|SURF_DRAWTURB ))
		return false;
	ti = R_BumpTextureIndex( t );
	if( ti < 0 || !R_BumpNormalMap( t, ti ) || bump.gloss[ti] < PROBE_GLOSS )
		return false;

	VectorClear( c );
	for( k = 0; k < s->polys->numverts; k++ )
		VectorAdd( c, s->polys->verts[k], c );
	VectorScale( c, 1.0f / s->polys->numverts, c );
	VectorCopy( s->plane->normal, n );
	if( FBitSet( s->flags, SURF_PLANEBACK ))
		VectorNegate( n, n );

	for( k = 48; k >= 8; k -= 8 )
	{
		VectorMA( c, k, n, p );
		if( !R_BumpInsideWall( p ))
			return true;
	}
	VectorCopy( c, p );
	return true;
}

/*
==============
R_BumpPlaceProbes

a point in front of each glossy surface, joining the close ones; then the
probes that reach every glossy surface (in front of it)
==============
*/
static void R_BumpPlaceProbes( void )
{
	int i, j;

	bump.surf_probe = Mem_Calloc( r_temppool, sizeof( short ) * PROBES_PER_SURF * WORLDMODEL->numsurfaces );

	for( i = 0; i < WORLDMODEL->numsurfaces; i++ )
	{
		vec3_t c, n, p, m, d;
		float best = PROBE_JOIN * PROBE_JOIN;
		int found = -1;

		if( !R_BumpProbePoint( &WORLDMODEL->surfaces[i], p, c, n ) || R_BumpInsideWall( p ))
			continue;

		for( j = 0; j < bump.numprobes; j++ )
		{
			vec3_t d;

			// only faces looking the same way, with the probe in front: the two sides of a
			// door were joined and the probe ended inside, so the outside did not reflect
			if( DotProduct( bump.probe_normal[j], n ) < 0.7f )
				continue;
			VectorSubtract( bump.probe_org[j], c, d );
			if( DotProduct( d, n ) < 1.0f )
				continue;
			VectorSubtract( bump.probe_org[j], p, d );
			if( DotProduct( d, d ) < best )
			{
				best = DotProduct( d, d );
				found = j;
			}
		}
		if( found < 0 )
		{
			if( bump.numprobes >= MAX_PROBES )
				continue;
			found = bump.numprobes++;
			VectorCopy( p, bump.probe_org[found] );
			VectorCopy( n, bump.probe_normal[found] );
			bump.probe_count[found] = 1;
			continue;
		}

		// move it towards the middle of its surfaces, if that is not inside a wall
		VectorLerp( bump.probe_org[found], 1.0f / ( bump.probe_count[found] + 1 ), p, m );
		VectorSubtract( m, c, d );
		if( !R_BumpInsideWall( m ) && DotProduct( d, n ) >= 1.0f )
			VectorCopy( m, bump.probe_org[found] );
		bump.probe_count[found]++;
	}

	for( i = 0; i < WORLDMODEL->numsurfaces; i++ )
	{
		const msurface_t *s = &WORLDMODEL->surfaces[i];
		short *list = bump.surf_probe + i * PROBES_PER_SURF;
		float key[PROBES_PER_SURF];
		vec3_t c, n, p, mins, maxs;
		float nearest = PROBE_REACH * PROBE_REACH;
		int count = 0, reach = 0, k, fallback = -1;

		if( !R_BumpProbePoint( s, p, c, n ))
			continue;

		ClearBounds( mins, maxs );
		for( k = 0; k < s->polys->numverts; k++ )
			AddPointToBounds( s->polys->verts[k], mins, maxs );

		// the ones closer than PROBE_RADIUS to some point of the face, nearest first
		// (to its box, and then to its middle: the first is used where none reaches)
		for( j = 0; j < bump.numprobes; j++ )
		{
			vec3_t d;
			float box = 0.0f, dd;

			VectorSubtract( bump.probe_org[j], c, d );
			if( DotProduct( d, n ) < 1.0f )
				continue;	// behind the surface
			dd = DotProduct( d, d );
			for( k = 0; k < 3; k++ )
			{
				float o = bump.probe_org[j][k] < mins[k] ? mins[k] - bump.probe_org[j][k] : bump.probe_org[j][k] > maxs[k] ? bump.probe_org[j][k] - maxs[k] : 0.0f;
				box += o * o;
			}
			if( dd < nearest )
			{
				nearest = dd;
				fallback = j;
			}
			if( box >= PROBE_RADIUS * PROBE_RADIUS )
				continue;

			dd = box + dd * 0.001f;
			reach++;
			if( count == PROBES_PER_SURF && dd >= key[count - 1] )
				continue;
			if( count < PROBES_PER_SURF )
				count++;
			for( k = count - 1; k > 0 && key[k - 1] > dd; k-- )
			{
				key[k] = key[k - 1];
				list[k] = list[k - 1];
			}
			key[k] = dd;
			list[k] = j + 1;
		}
		if( !count && fallback >= 0 )
			list[0] = fallback + 1;
		if( reach > PROBES_PER_SURF )
			bump.too_many++;
	}
}

static GLuint R_BumpDynamicTexture( int w, int h, GLenum ifmt, GLenum fmt, GLenum type, GLenum filter )
{
	GLuint t;

	pglGenTextures( 1, &t );
	pglBindTexture( GL_TEXTURE_2D, t );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	pglTexImage2D( GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, NULL );
	return t;
}

/*
==============
R_BumpDynamic

called before a frame is drawn: some of the six sides around the eye, again
==============
*/
void R_BumpDynamic( const ref_viewpass_t *rvp )
{
	int i, n, size;

	if( dyn.drawing || bump.capturing )
		return;
	if( !r_reflect.value || !r_reflect_dynamic.value || !R_PostRunning( ))
		return;
	if( !WORLDMODEL || !FBitSet( rvp->flags, RF_DRAW_WORLD ) || FBitSet( rvp->flags, RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return;
	if( rvp->viewport[2] != gpGlobals->width || rvp->viewport[3] != gpGlobals->height )
		return;

	for( size = 64; size < r_reflect_dynamic_size.value && size < 512; size <<= 1 );
	if( size > gpGlobals->width || size > gpGlobals->height )
		return;
	if( !dyn.color[0] || dyn.size != size )
	{
		for( i = 0; i < 2; i++ )
		{
			if( dyn.color[i] ) pglDeleteTextures( 1, &dyn.color[i] );
			if( dyn.depth[i] ) pglDeleteTextures( 1, &dyn.depth[i] );
		}
		if( dyn.fcolor ) pglDeleteTextures( 1, &dyn.fcolor );
		if( dyn.fdepth ) pglDeleteTextures( 1, &dyn.fdepth );
		if( dyn.fbo ) pglDeleteFramebuffers( 1, &dyn.fbo );
		for( i = 0; i < 2; i++ )
		{
			dyn.color[i] = R_BumpDynamicTexture( size * 4, size * 2, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR );
			dyn.depth[i] = R_BumpDynamicTexture( size * 4, size * 2, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST );
		}
		dyn.fcolor = R_BumpDynamicTexture( size, size, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST );
		dyn.fdepth = R_BumpDynamicTexture( size, size, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST );
		pglGenFramebuffers( 1, &dyn.fbo );
		pglBindFramebuffer( GL_FRAMEBUFFER, dyn.fbo );
		pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dyn.fcolor, 0 );
		pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dyn.fdepth, 0 );
		if( pglCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
		{
			gEngfuncs.Con_Printf( S_ERROR "r_reflect_dynamic: framebuffer incomplete\n" );
			pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
			pglDeleteFramebuffers( 1, &dyn.fbo );
			dyn.fbo = 0;
		}
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glState.currentTextures[glState.activeTMU] = -1;
		glState.currentTexturesIndex[glState.activeTMU] = -1;
		dyn.size = size;
		dyn.valid = false;
		dyn.next = 0;
		dyn.front = 0;
	}
	if( !dyn.fbo )
		return;

	// a few sides a frame, also while moving: all six each frame of walking (each one the whole view again)
	// took the frame from over 150 to under 40 with players around; only a jump (respawn, teleport) does all
	n = bound( 1, (int)r_reflect_dynamic_sides.value, 6 );
	if( !dyn.valid || VectorDistance( dyn.origin[dyn.front], rvp->vieworigin ) >= 64.0f )
	{
		n = 6;	// all at once, from here
		dyn.next = 0;
	}
	if( !r_reflect_cube.value )
		n = 0;	// only the mirrors
	dyn.drawing = true;
	for( i = 0; i < n; i++ )
	{
		int j = dyn.next, b = dyn.front ^ 1;

		if( j == 0 )
			VectorCopy( rvp->vieworigin, dyn.origin[b] );	// all the sides of this one from here
		dyn.next = ( dyn.next + 1 ) % 6;
		pglBindFramebuffer( GL_FRAMEBUFFER, dyn.fbo );
		R_DrawCubemapView( dyn.origin[b], probe_angles[j], size );
		dyn.zfar[b][j] = Q_max( 256.0f, RI.farClip );
		pglBindFramebuffer( GL_FRAMEBUFFER, dyn.fbo );	// still the side: the copies read from it
		pglBindTexture( GL_TEXTURE_2D, dyn.color[b] );
		pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, ( j % 4 ) * size, ( j / 4 ) * size, 0, 0, size, size );
		pglBindTexture( GL_TEXTURE_2D, dyn.depth[b] );
		pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, ( j % 4 ) * size, ( j / 4 ) * size, 0, 0, size, size );
		if( dyn.next == 0 )
		{
			dyn.front = b;	// complete: the reflections read it now
			dyn.valid = true;
		}
	}
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	dyn.drawing = false;
	glState.currentTextures[glState.activeTMU] = -1;
	glState.currentTexturesIndex[glState.activeTMU] = -1;

	R_BumpPlanes( rvp );
}

/*
==============
R_BumpPlanes

the mirrors: the biggest planes seen in the last frame, drawn from the eye reflected on them,
cutting what is behind the plane
==============
*/
/*
==============
R_BumpBounces

the second bounce: for each pair of mirrors (k, j), the view of mirror k through mirror j
(its eye reflected again on j), put on the image of mirror k where it sees j
==============
*/
static void R_BumpBounces( int w, int h, float fov, float ty )
{
	int k, j, npares = 0;
	float zfar2 = 4096.0f;
	qboolean ok;

	if( !planar.fbo2 )
	{
		planar.color2 = R_BumpDynamicTexture( w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR );
		planar.depth2 = R_BumpDynamicTexture( w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST );
		planar.copy = R_BumpDynamicTexture( w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST );
		pglGenFramebuffers( 1, &planar.fbo2 );
		pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo2 );
		pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, planar.color2, 0 );
		pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, planar.depth2, 0 );
		if( pglCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
		{
			gEngfuncs.Con_Printf( S_ERROR "r_reflect_bounces: framebuffer incomplete\n" );
			pglDeleteFramebuffers( 1, &planar.fbo2 );
			planar.fbo2 = 0;
			return;
		}
	}

	for( k = 0; k < planar.count; k++ )
	{
		qboolean copied = false;

		for( j = 0; j < planar.count; j++ )
		{
			ref_viewpass_t v;
			vec3_t f, r, u, eye2, vf2, vr2, vu2;
			float hgt;

			if( j == k )
				continue;
			// the eye of mirror k has to be in front of mirror j (a floor seen from under another: no)
			hgt = DotProduct( planar.plane[j], planar.eye[k] ) - planar.plane[j][3];
			if( hgt < 1.0f )
				continue;

			memset( &v, 0, sizeof( v ));
			SetBits( v.flags, RF_DRAW_WORLD|RF_DRAW_CUBEMAP );
			v.viewport[2] = w;
			v.viewport[3] = h;
			v.fov_x = fov;
			v.fov_y = atan( ty ) * 360.0f / M_PI_F;
			VectorMA( planar.eye[k], -2.0f * hgt, planar.plane[j], v.vieworigin );
			AngleVectors( planar.ang[k], f, r, u );
			VectorMA( f, -2.0f * DotProduct( f, planar.plane[j] ), planar.plane[j], f );
			v.viewangles[PITCH] = -asin( bound( -1.0f, f[2], 1.0f )) * 180.0f / M_PI_F;
			v.viewangles[YAW] = atan2( f[1], f[0] ) * 180.0f / M_PI_F;
			v.viewangles[ROLL] = 0.0f;

			Vector4Copy( planar.plane[j], planar.clip );
			VectorMA( planar.maineye, 4.0f - ( DotProduct( planar.plane[j], planar.maineye ) - planar.plane[j][3] ), planar.plane[j], planar.pvsfoot );
			pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo2 );
			RI.viewleaf = NULL;
			R_RenderFrame( &v );
			RI.viewleaf = NULL;
			pglDisable( GL_CLIP_PLANE0 );
			zfar2 = Q_max( 256.0f, RI.farClip );
			VectorCopy( v.vieworigin, eye2 );
			VectorCopy( RI.vforward, vf2 );
			VectorCopy( RI.vright, vr2 );
			VectorCopy( RI.vup, vu2 );

			// the image of mirror k as it was, to read from while writing on it
			if( !copied )
			{
				pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo[k] );
				GL_SelectTexture( XASH_TEXTURE0 );
				pglBindTexture( GL_TEXTURE_2D, planar.copy );
				pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h );
				glState.currentTextures[glState.activeTMU] = -1;
				copied = true;
			}
			// its depth is read while drawing on it: out of the framebuffer meanwhile
			pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo[k] );
			pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
			ok = R_PostMirrorBounce( planar.fbo[k], planar.copy, planar.depth[k], w, h, planar.eye[k], planar.vf[k], planar.vr[k], planar.vu[k],
				planar.tanx, planar.tany, planar.zfar[k], planar.plane[j], planar.color2, planar.depth2, zfar2, eye2, vf2, vr2, vu2, planar.pblur[j] );
			pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo[k] );
			pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, planar.depth[k], 0 );
			if( r_reflect_debug.value )
				gEngfuncs.Con_NPrintf( 18 + npares, "bounce: mirror %d shows mirror %d (eye %.0f %.0f %.0f)", k, j, eye2[0], eye2[1], eye2[2] );
			npares++;
			if( !ok )
				return;
		}
	}
}

static void R_BumpPlanes( const ref_viewpass_t *rvp )
{
	VectorCopy( rvp->vieworigin, mirada_org );
	VectorCopy( rvp->viewangles, mirada_ang );
	if( r_reflect_medir.value )
	{
		double now = gEngfuncs.pfnTime();
		if( planar.tframe > 0.0 && planar.nframes >= 300 )
		{
			gEngfuncs.Con_Printf( "medir: frame %.2f ms, mirrors %.2f ms (%.1f mirrors, %.1f reused)\n", ( now - planar.tframe ) * 1000.0 / planar.nframes,
				planar.tmir * 1000.0 / planar.nframes, (float)planar.nmirrors / planar.nframes, (float)planar.nreused / planar.nframes );
			planar.nreused = 0;
			planar.nframes = 0;
			planar.nmirrors = 0;
			planar.tmir = 0.0;
			planar.tframe = now;
		}
		if( planar.tframe <= 0.0 ) planar.tframe = now;
		planar.nframes++;
	}
	else planar.tframe = 0.0;

	int w = gpGlobals->width, h = gpGlobals->height;	// full size, they are seen like a mirror
	int i, j, k, want = bound( 0, (int)r_reflect_planes.value, MAX_PLANES );
	float tx, ty, fov;
	vec4_t chosen[MAX_PLANES];
	float chosenblur[MAX_PLANES];
	double chosensince[MAX_PLANES];
	int nchosen = 0;

	// the ones already drawn keep their place unless another is clearly bigger (no jumping between two),
	// and for a second after they got it, whatever: with many metal walls (ns_ayumi) they came and went
	double candsince[PLANE_CANDIDATES];
	for( i = 0; i < planar.ncand; i++ )
	{
		candsince[i] = gp_host->realtime;
		for( j = 0; j < planar.count; j++ )
			if( DotProduct( planar.cand[i], planar.plane[j] ) > 0.999f && fabs( planar.cand[i][3] - planar.plane[j][3] ) < 1.0f )
			{
				planar.weight[i] *= gp_host->realtime - planar.since[j] < 1.0 ? 1000.0f : 4.0f;
				candsince[i] = planar.since[j];
			}
	}
	for( k = 0; k < want; k++ )
	{
		int best = -1;
		for( i = 0; i < planar.ncand; i++ )
			if( planar.weight[i] > 0.0f && planar.area[i] >= PLANE_MIN_AREA && ( best < 0 || planar.weight[i] > planar.weight[best] ))
				best = i;
		if( best < 0 )
			break;
		Vector4Copy( planar.cand[best], chosen[nchosen] );
		chosenblur[nchosen] = planar.blur[best];
		chosensince[nchosen] = candsince[best];
		nchosen++;
		planar.weight[best] = 0.0f;
	}
	if( r_reflect_debug.value )
	{
		gEngfuncs.Con_NPrintf( 4, "mirrors %d, candidates %d", nchosen, planar.ncand );
		for( i = 0, j = 0; i < planar.ncand && j < 12; i++ )
		{
			if( planar.area[i] < PLANE_MIN_AREA )
				continue;
			gEngfuncs.Con_NPrintf( 5 + j++, "%s n %.2f %.2f %.2f d %.0f area %.0f", planar.weight[i] == 0.0f ? "MIRROR" : "      ",
				planar.cand[i][0], planar.cand[i][1], planar.cand[i][2], planar.cand[i][3], planar.area[i] );
		}
		for( ; j < 12; j++ )
			gEngfuncs.Con_NPrintf( 5 + j, " " );
	}
	planar.ncand = 0;
	planar.count = 0;
	planar.frame++;
	if( !nchosen || w < 16 || h < 16 )
		return;

	if( !planar.fbo[0] || planar.w != w || planar.h != h )
	{
		for( i = 0; i < MAX_PLANES; i++ )
		{
			if( planar.color[i] ) pglDeleteTextures( 1, &planar.color[i] );
			if( planar.depth[i] ) pglDeleteTextures( 1, &planar.depth[i] );
			if( planar.fbo[i] ) pglDeleteFramebuffers( 1, &planar.fbo[i] );
			planar.color[i] = R_BumpDynamicTexture( w, h, GL_RGBA16F_ARB, GL_RGBA, GL_HALF_FLOAT_ARB, GL_LINEAR );	// the depth goes in the alpha
			// with its smaller copies, for the blur of rough mirrors (gl_post.c)
			pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			if( pglGenerateMipmap ) pglGenerateMipmap( GL_TEXTURE_2D );
			planar.depth[i] = R_BumpDynamicTexture( w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST );
			pglGenFramebuffers( 1, &planar.fbo[i] );
			pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo[i] );
			pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, planar.color[i], 0 );
			pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, planar.depth[i], 0 );
			if( pglCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			{
				gEngfuncs.Con_Printf( S_ERROR "r_reflect_planes: framebuffer incomplete\n" );
				pglDeleteFramebuffers( 1, &planar.fbo[i] );
				planar.fbo[i] = 0;
			}
		}
		if( planar.fbo2 )
		{
			pglDeleteTextures( 1, &planar.color2 );
			pglDeleteTextures( 1, &planar.depth2 );
			pglDeleteTextures( 1, &planar.copy );
			pglDeleteFramebuffers( 1, &planar.fbo2 );
			planar.fbo2 = planar.color2 = planar.depth2 = planar.copy = 0;
		}
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glState.currentTextures[glState.activeTMU] = -1;
		glState.currentTexturesIndex[glState.activeTMU] = -1;
		planar.w = w;
		planar.h = h;
		memset( planar.valid, 0, sizeof( planar.valid ));
	}

	// a little wider than the view: the reflected view may be turned half a turn, it covers the same
	fov = Q_min( rvp->fov_x + 6.0f, 170.0f );
	tx = tan( fov * M_PI_F / 360.0f );
	ty = tan( rvp->fov_y * M_PI_F / 360.0f ) * tx / tan( rvp->fov_x * M_PI_F / 360.0f );

	VectorCopy( rvp->vieworigin, planar.maineye );
	if( r_reflect_medir.value == 1.0f )	// 2: only the frame, without stopping to wait for the card
	{
		pglFinish();
		planar.t0 = gEngfuncs.pfnTime();
	}
	planar.drawing = true;
	for( k = 0; k < nchosen; k++ )
	{
		ref_viewpass_t v;
		vec3_t f, r, u;
		float hgt;

		if( !planar.fbo[planar.count] )
			continue;
		hgt = DotProduct( chosen[k], rvp->vieworigin ) - chosen[k][3];
		if( hgt < 1.0f )
			continue;

		memset( &v, 0, sizeof( v ));
		SetBits( v.flags, RF_DRAW_WORLD|RF_DRAW_CUBEMAP );
		v.viewport[2] = w;
		v.viewport[3] = h;
		v.fov_x = fov;
		v.fov_y = atan( ty ) * 360.0f / M_PI_F;
		VectorMA( rvp->vieworigin, -2.0f * hgt, chosen[k], v.vieworigin );
		AngleVectors( rvp->viewangles, f, r, u );
		VectorMA( f, -2.0f * DotProduct( f, chosen[k] ), chosen[k], f );
		v.viewangles[PITCH] = -asin( bound( -1.0f, f[2], 1.0f )) * 180.0f / M_PI_F;
		v.viewangles[YAW] = atan2( f[1], f[0] ) * 180.0f / M_PI_F;
		v.viewangles[ROLL] = 0.0f;

		// the same plane in the same place, the reflected eye barely moved: the image of a frame before
		// still does (every r_reflect_cada frames, each place on its turn)
		{
			int s = planar.count, cada = Q_max( 1, (int)r_reflect_cada.value );

			if( r_reflect_bounces.value >= 2.0f )
				cada = 1;	// the second bounce is drawn over the image: again and again on an old one

			if( cada > 1 && planar.valid[s] && ( planar.frame + s ) % cada
				&& DotProduct( chosen[k], planar.plane[s] ) > 0.9999f && fabs( chosen[k][3] - planar.plane[s][3] ) < 0.5f
				&& VectorDistance( v.vieworigin, planar.eye[s] ) < 24.0f && DotProduct( f, planar.vf[s] ) > 0.995f )
			{
				planar.pblur[s] = chosenblur[k];
				planar.since[s] = chosensince[k];
				planar.pfade[s] = bound( 0.0f, ( gp_host->realtime - chosensince[k] ) / 0.4f, 1.0f );
				planar.count++;
				planar.nreused++;
				continue;
			}
		}

		Vector4Copy( chosen[k], planar.plane[planar.count] );
		planar.pblur[planar.count] = chosenblur[k];
		planar.since[planar.count] = chosensince[k];
		planar.pfade[planar.count] = bound( 0.0f, ( gp_host->realtime - chosensince[k] ) / 0.4f, 1.0f );	// comes in softly
		planar.current = planar.count;
		Vector4Copy( chosen[k], planar.clip );
		VectorMA( rvp->vieworigin, 4.0f - hgt, chosen[k], planar.pvsfoot );
		pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo[planar.count] );
		RI.viewleaf = NULL;
		R_RenderFrame( &v );
		RI.viewleaf = NULL;		// force markleafs next frame
		R_PostMirrorDepth( planar.fbo[planar.count], planar.depth[planar.count], w, h, Q_max( 256.0f, RI.farClip ));
		if( pglGenerateMipmap )
		{
			GL_SelectTexture( XASH_TEXTURE0 );
			pglBindTexture( GL_TEXTURE_2D, planar.color[planar.count] );
			pglGenerateMipmap( GL_TEXTURE_2D );
			glState.currentTextures[glState.activeTMU] = -1;
		}
		pglDisable( GL_CLIP_PLANE0 );

		// how it was seen, for gl_post.c
		VectorCopy( v.vieworigin, planar.eye[planar.count] );
		VectorCopy( RI.vforward, planar.vf[planar.count] );
		VectorCopy( RI.vright, planar.vr[planar.count] );
		VectorCopy( RI.vup, planar.vu[planar.count] );
		VectorCopy( v.viewangles, planar.ang[planar.count] );
		planar.zfar[planar.count] = Q_max( 256.0f, RI.farClip );
		planar.valid[planar.count] = true;
		if( r_reflect_debug.value >= 2 )
		{
			float dz[3] = { 0 };
			pglReadPixels( w / 2, h / 4, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &dz[0] );
			pglReadPixels( w / 2, h / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &dz[1] );
			pglReadPixels( w / 2, 3 * h / 4, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &dz[2] );
			gEngfuncs.Con_NPrintf( 20 + planar.count, "mirror %d depth %.5f %.5f %.5f zfar %.0f", planar.count, dz[0], dz[1], dz[2], planar.zfar[planar.count] );
		}
		planar.count++;
	}
	planar.tanx = tx;
	planar.tany = ty;
	planar.nmirrors += planar.count;
	if( r_reflect_bounces.value >= 2.0f && planar.count > 1 )
		R_BumpBounces( w, h, fov, ty );
	planar.drawing = false;
	if( r_reflect_medir.value == 1.0f )
	{
		pglFinish();
		planar.tmir += gEngfuncs.pfnTime() - planar.t0;
	}
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glState.currentTextures[glState.activeTMU] = -1;
	glState.currentTexturesIndex[glState.activeTMU] = -1;
}

// R_SetupGL, with the view matrix loaded: in a mirror, cut what is behind its plane
void R_BumpClipPlane( void )
{
	if( planar.drawing )
	{
		const float *p = planar.clip;
		GLdouble eq[4] = { p[0], p[1], p[2], -p[3] - 0.5 };

		pglClipPlane( GL_CLIP_PLANE0, eq );
		pglEnable( GL_CLIP_PLANE0 );
	}
	else pglDisable( GL_CLIP_PLANE0 );
}

// r_debug_vista: the main view from another place
const ref_viewpass_t *R_BumpDebugView( const ref_viewpass_t *rvp )
{
	static ref_viewpass_t v;
	float x, y, z, p, w;

	if( !r_debug_vista.string[0] || !FBitSet( rvp->flags, RF_DRAW_WORLD ) || FBitSet( rvp->flags, RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return rvp;
	if( sscanf( r_debug_vista.string, "%f %f %f %f %f", &x, &y, &z, &p, &w ) != 5 )
		return rvp;
	v = *rvp;
	VectorSet( v.vieworigin, x, y, z );
	VectorSet( v.viewangles, p, w, 0.0f );
	return &v;
}

qboolean R_BumpCubeOn( void )
{
	return r_reflect_cube.value != 0.0f;
}

const float *R_BumpPlanarEye( void )
{
	return planar.maineye;
}

const float *R_BumpPlanarFoot( void )
{
	return planar.pvsfoot;
}

qboolean R_BumpDrawingPlanar( void )
{
	return planar.drawing;
}

// for gl_post.c: the mirrors of this frame
int R_BumpMirrors( GLuint *tex, GLuint *dep, float *zfar, vec4_t *plane, vec3_t *eye, vec3_t *vf, vec3_t *vr, vec3_t *vu, float *blur, float *fade, float *tanx, float *tany )
{
	int i;

	if( !r_reflect.value || !r_reflect_dynamic.value )
		return 0;
	for( i = 0; i < planar.count; i++ )
	{
		tex[i] = planar.color[i];
		dep[i] = planar.depth[i];
		zfar[i] = planar.zfar[i];
		Vector4Copy( planar.plane[i], plane[i] );
		VectorCopy( planar.eye[i], eye[i] );
		VectorCopy( planar.vf[i], vf[i] );
		VectorCopy( planar.vr[i], vr[i] );
		VectorCopy( planar.vu[i], vu[i] );
		blur[i] = planar.pblur[i];
		fade[i] = planar.pfade[i];
	}
	*tanx = planar.tanx;
	*tany = planar.tany;
	return planar.count;
}

qboolean R_BumpDynamicOn( void )
{
	return r_reflect.value && r_reflect_dynamic.value;
}

// drawing the sides around the player: players are drawn there (not the local one)
qboolean R_BumpDrawingDynamic( void )
{
	return dyn.drawing || planar.drawing;
}

// for gl_post.c: the sides around the player, where they were drawn from
qboolean R_BumpDynamicCube( GLuint *color, GLuint *depth, vec3_t origin, float zfar[6], int *size )
{
	if( !dyn.valid || !r_reflect.value || !r_reflect_dynamic.value )
		return false;
	*color = dyn.color[dyn.front];
	*depth = dyn.depth[dyn.front];
	VectorCopy( dyn.origin[dyn.front], origin );
	memcpy( zfar, dyn.zfar[dyn.front], sizeof( dyn.zfar[0] ));
	*size = dyn.size;
	return true;
}

/*
==============
R_BumpProbes

called before a frame is drawn: on the first one of a map, takes the probes
==============
*/
void R_BumpProbes( const ref_viewpass_t *rvp )
{
	int i, j;

	if( bump.capturing || !r_reflect.value || !r_reflect_probes.value )
		return;
	if( !WORLDMODEL || !FBitSet( rvp->flags, RF_DRAW_WORLD ) || FBitSet( rvp->flags, RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return;
	if( !R_BumpProgram( ))
		return;
	R_BumpCheckWorld();
	if( bump.probes_done )
		return;
	bump.probes_done = true;
	if( gpGlobals->width < PROBE_SIZE || gpGlobals->height < PROBE_SIZE )
		return;

	R_BumpPlaceProbes();

	bump.probe_tex = GL_CreateTexture( "*bumpprobes", PROBE_COLS * PROBE_SIZE * 4, ( MAX_PROBES / PROBE_COLS ) * PROBE_SIZE * 2, NULL, TF_IMAGE );
	if( !bump.probe_tex )
	{
		bump.numprobes = 0;
		return;
	}

	bump.capturing = true;
	for( i = 0; i < bump.numprobes; i++ )
	{
		int x = ( i % PROBE_COLS ) * PROBE_SIZE * 4, y = ( i / PROBE_COLS ) * PROBE_SIZE * 2;

		for( j = 0; j < 6; j++ )
		{
			R_DrawCubemapView( bump.probe_org[i], probe_angles[j], PROBE_SIZE );
			GL_Bind( XASH_TEXTURE0, bump.probe_tex );
			pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, x + ( j % 4 ) * PROBE_SIZE, y + ( j / 4 ) * PROBE_SIZE, 0, 0, PROBE_SIZE, PROBE_SIZE );
		}
	}
	bump.capturing = false;

	// the average brightness of each, read back all at once
	{
		int w = PROBE_COLS * PROBE_SIZE * 4, h = ( MAX_PROBES / PROBE_COLS ) * PROBE_SIZE * 2;
		byte *px = Mem_Malloc( r_temppool, w * h * 4 );

		GL_Bind( XASH_TEXTURE0, bump.probe_tex );
		pglGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px );
		for( i = 0; i < bump.numprobes; i++ )
		{
			int x0 = ( i % PROBE_COLS ) * PROBE_SIZE * 4, y0 = ( i / PROBE_COLS ) * PROBE_SIZE * 2, x, y;
			double sum = 0.0;

			for( y = y0; y < y0 + PROBE_SIZE * 2; y++ )
			{
				for( x = x0; x < x0 + PROBE_SIZE * 4; x++ )
				{
					const byte *p = px + ( y * w + x ) * 4;
					sum += p[0] * 0.3 + p[1] * 0.59 + p[2] * 0.11;
				}
			}
			// not too close to black (a dark probe would blow up)
			bump.probe_avg[i] = Q_max( 0.05f, sum / ( PROBE_SIZE * PROBE_SIZE * 8 ) / 255.0 );
		}
		Mem_Free( px );
	}

	gEngfuncs.Con_Reportf( "r_bump: %d reflection probes (%d faces reached by more than %d)\n", bump.numprobes, bump.too_many, PROBES_PER_SURF );
}

static void R_BumpObjectMatrix( void )
{
	const matrix4x4 *m = &RI.objectMatrix;

	pglUniform4fARB( bump.u_r0, (*m)[0][0], (*m)[0][1], (*m)[0][2], (*m)[0][3] );
	pglUniform4fARB( bump.u_r1, (*m)[1][0], (*m)[1][1], (*m)[1][2], (*m)[1][3] );
	pglUniform4fARB( bump.u_r2, (*m)[2][0], (*m)[2][1], (*m)[2][2], (*m)[2][3] );
}

/*
==============
R_BumpBegin

before the surfaces of texture t: shader on if it has a normal map
==============
*/
qboolean R_BumpBegin( texture_t *t, int index )
{
	const movevars_t *mv = gp_movevars;
	vec3_t sun;
	int normalmap;

	bump.active = false;
	bump.current = t;
	if( !R_BumpWanted() || bump.capturing || R_BumpSkipTexture( t ))
		return false;
	if( RI.currententity && RI.currententity->curstate.rendermode != kRenderNormal )
		return false;
	if( t->texturechain && FBitSet( t->texturechain->flags, SURF_DRAWTURB|SURF_TRANSPARENT ))
		return false;
	if( !R_BumpProgram( ))
		return false;
	normalmap = R_BumpNormalMap( t, index );
	if( !normalmap )
		return false;

	R_BumpSky();

	VectorSet( sun, -mv->skyvec[0], -mv->skyvec[1], -mv->skyvec[2] );
	if( VectorNormalizeLength( sun ) < 0.01f )
		VectorSet( sun, 0.3f, 0.2f, 0.93f );

	GL_Bind( XASH_TEXTURE2, normalmap );
	GL_SelectTexture( XASH_TEXTURE0 );
	pglUseProgramObjectARB( bump.prog );
	pglUniform3fARB( bump.u_sun, sun[0], sun[1], sun[2] );
	pglUniform3fARB( bump.u_eye, RI.rvp.vieworigin[0], RI.rvp.vieworigin[1], RI.rvp.vieworigin[2] );
	pglUniform3fARB( bump.u_hor, bump.horizon[0], bump.horizon[1], bump.horizon[2] );
	pglUniform3fARB( bump.u_zen, bump.zenith[0], bump.zenith[1], bump.zenith[2] );
	pglUniform3fARB( bump.u_suncol, mv->skycolor[0] / 255.0f, mv->skycolor[1] / 255.0f, mv->skycolor[2] / 255.0f );
	{
		int f = bump.flags[index];

		pglUniform1fARB( bump.u_bump, r_bump.value && FBitSet( f, BUMP_RELIEF ) ? r_bump_scale.value : 0.0f );	// 0: the flat face
		pglUniform1fARB( bump.u_spec, FBitSet( f, BUMP_REFLECT ) ? r_specular.value * bump.brillok[index] : 0.0f );
		pglUniform1fARB( bump.u_refl, FBitSet( f, BUMP_REFLECT ) ? r_reflect.value * bump.brillok[index] : 0.0f );
		bump.reflects = FBitSet( f, BUMP_REFLECT ) && r_reflect.value;
		bump.curblur = bump.blur[index];
		bump.curplano = FBitSet( f, BUMP_PLANO ) != 0;
		pglUniform1fARB( bump.u_plano, bump.curplano ? 1.0f : 0.0f );
		pglUniform1fARB( bump.u_rug, bound( 0.0f, bump.blur[index] / RUGOSIDAD_MAX, 1.0f ));
	}
	// the mirrors are drawn before the post processing starts: they mark what reflects too (the second bounce)
	pglUniform1fARB( bump.u_ssr, R_PostScreenReflections( ) || planar.drawing ? 1.0f : 0.0f );
	pglUniform1fARB( bump.u_nprobes, -1.0f );	// no fixed reflection unless the surface has probes (not the sky either)
	// the sky gradient, relative to its own average too
	pglUniform1fARB( bump.u_skyavg, Q_max( 0.05f, ( bump.horizon[0] + bump.horizon[1] + bump.horizon[2] + bump.zenith[0] + bump.zenith[1] + bump.zenith[2] ) / 6.0f ));
	if( r_reflect_probes.value && bump.probe_tex )
	{
		GL_Bind( XASH_TEXTURE3, bump.probe_tex );
		GL_SelectTexture( XASH_TEXTURE0 );
	}
	R_BumpObjectMatrix();
	bump.bound_list = NULL;
	bump.active = true;
	return true;
}

/*
==============
R_BumpPlaneSeen

a face that reflects, drawn in the view: its plane, for the mirrors of the next frame
==============
*/
// faces behind walls: the engine draws what is in the PVS and in front, hidden or not; from outside the
// floors inside the bases counted, and being big they took the mirrors from the door. Lines from the eye
// to the middle and around it; the answer kept some frames (each face its turn, not all at once)
static struct
{
	int		*frame;		// when it was looked at, 0 never
	byte		*seen;
	int		count;
	const model_t	*world;
} surfvis;

static qboolean R_BumpFaceSeen( const msurface_t *s, const vec3_t N, float d, const vec3_t c, const vec3_t ofs )
{
	int i, k = s - WORLDMODEL->surfaces;
	qboolean seen = false;

	if( surfvis.world != WORLDMODEL || surfvis.count != WORLDMODEL->numsurfaces )
	{
		if( surfvis.frame ) Mem_Free( surfvis.frame );
		if( surfvis.seen ) Mem_Free( surfvis.seen );
		surfvis.count = WORLDMODEL->numsurfaces;
		surfvis.frame = Mem_Calloc( r_temppool, sizeof( int ) * surfvis.count );
		surfvis.seen = Mem_Calloc( r_temppool, surfvis.count );
		surfvis.world = WORLDMODEL;
	}
	if( k >= 0 && k < surfvis.count && surfvis.frame[k] && tr.realframecount - surfvis.frame[k] < 6 )
		return surfvis.seen[k];

	for( i = 0; i < 9 && !seen; i++ )
	{
		vec3_t p, eye;
		pmtrace_t *t;
		float len;

		VectorCopy( c, p );
		if( i )
		{
			// a corner of its box, on the plane, most of the way from the middle
			int j = i - 1;
			vec3_t q;

			VectorSet( q, ( j & 1 ? s->info->maxs : s->info->mins )[0], ( j & 2 ? s->info->maxs : s->info->mins )[1], ( j & 4 ? s->info->maxs : s->info->mins )[2] );
			VectorAdd( q, ofs, q );
			VectorMA( q, -( DotProduct( N, q ) - d ), N, q );
			VectorLerp( c, 0.8f, q, p );
		}
		VectorMA( p, 2.0f, N, p );
		VectorCopy( RI.rvp.vieworigin, eye );
		t = gEngfuncs.EV_VisTraceLine( eye, p, PM_GLASS_IGNORE|PM_STUDIO_IGNORE );
		len = VectorDistance( eye, p );
		seen = ( 1.0f - t->fraction ) * len < 4.0f;
	}

	if( k >= 0 && k < surfvis.count )
	{
		surfvis.frame[k] = tr.realframecount - ( k % 6 );	// spread: not all looked at again the same frame
		if( !surfvis.frame[k] ) surfvis.frame[k] = 1;
		surfvis.seen[k] = seen;
	}
	return seen;
}

static void R_BumpPlaneSeen( const msurface_t *s, const vec3_t N )
{
	vec3_t c, size, ofs = { 0.0f, 0.0f, 0.0f };
	float d, h, area, w;
	int i;

	if( FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ) || !R_PostScreenReflections( ) || !s->info )
		return;

	d = FBitSet( s->flags, SURF_PLANEBACK ) ? -s->plane->dist : s->plane->dist;
	VectorCopy( s->info->origin, c );
	if( RI.currententity && RI.currententity != CL_GetEntityByIndex( 0 ))
	{
		Matrix4x4_VectorTransform( RI.objectMatrix, s->info->origin, c );
		VectorSet( ofs, RI.objectMatrix[0][3], RI.objectMatrix[1][3], RI.objectMatrix[2][3] );
		d += DotProduct( N, ofs );
	}
	h = DotProduct( N, RI.rvp.vieworigin ) - d;
	if( h < 1.0f )
		return;

	// not the ceilings (the floor texture of the bases is on them too); the slopes do: with the weight of
	// what they take of the view and the mirror kept by the one that has it, they no longer take it from the floor
	if( N[2] < -0.1f )
		return;

	if( !R_BumpFaceSeen( s, N, d, c, ofs ))
		return;

	// the part of the view it takes: its size, turned to the eye, over the distance squared. Over the
	// distance alone the big floors far away won over the door in front, which kept only the fixed reflection
	VectorSubtract( s->info->maxs, s->info->mins, size );
	area = Q_max( size[0] * size[1], Q_max( size[0] * size[2], size[1] * size[2] ));
	VectorSubtract( RI.rvp.vieworigin, c, c );
	{
		float dist = VectorLength( c ) + 1.0f;
		w = area * Q_max( h / dist, 0.15f ) / ( dist * dist + 64.0f * 64.0f ) * 1000.0f;
	}

	for( i = 0; i < planar.ncand; i++ )
	{
		if( DotProduct( planar.cand[i], N ) > 0.999f && fabs( planar.cand[i][3] - d ) < 1.0f )
		{
			planar.weight[i] += w;
			planar.area[i] += area;
			planar.blur[i] = Q_max( planar.blur[i], bump.curblur );
			return;
		}
	}
	if( planar.ncand >= PLANE_CANDIDATES )
		return;
	VectorCopy( N, planar.cand[i] );
	planar.cand[i][3] = d;
	planar.weight[i] = w;
	planar.area[i] = area;
	planar.blur[i] = bump.curblur;
	planar.ncand++;
}

/*
==============
R_BumpAguaPlano

the water of the world asks for a mirror too, sharp (gl_agua.c)
==============
*/
void R_BumpAguaPlano( const msurface_t *s, const vec3_t N )
{
	float blur = bump.curblur;

	bump.curblur = 0.0f;
	R_BumpPlaneSeen( s, N );
	bump.curblur = blur;
}

/*
==============
R_BumpSurface

the directions of one face (its texture axes made perpendicular to its plane)
and its probes
==============
*/
void R_BumpSurface( const msurface_t *s )
{
	vec3_t N, T, B, c;
	vec4_t po[PROBES_PER_SURF];
	vec2_t pc[PROBES_PER_SURF];
	const short *list;
	float d;
	int i, n;

	if( !bump.active )
		return;

	VectorCopy( s->plane->normal, N );
	if( FBitSet( s->flags, SURF_PLANEBACK ))
		VectorNegate( N, N );

	VectorCopy( s->texinfo->vecs[0], T );
	d = DotProduct( T, N );
	VectorMA( T, -d, N, T );
	if( VectorNormalizeLength( T ) < 0.0001f )
		return;

	CrossProduct( N, T, c );
	VectorCopy( s->texinfo->vecs[1], B );
	if( DotProduct( c, B ) < 0.0f )
		VectorNegate( c, c );
	VectorCopy( c, B );

	// doors and the like: their axes turned with them
	if( RI.currententity && RI.currententity != CL_GetEntityByIndex( 0 ))
	{
		vec3_t r;
		Matrix4x4_VectorRotate( RI.objectMatrix, T, r ); VectorCopy( r, T );
		Matrix4x4_VectorRotate( RI.objectMatrix, B, r ); VectorCopy( r, B );
		Matrix4x4_VectorRotate( RI.objectMatrix, N, r ); VectorCopy( r, N );
	}

	if( bump.reflects && bump.curplano )
		R_BumpPlaneSeen( s, N );

	pglUniform3fARB( bump.u_T, T[0], T[1], T[2] );
	pglUniform3fARB( bump.u_B, B[0], B[1], B[2] );
	pglUniform3fARB( bump.u_N, N[0], N[1], N[2] );

	if( !r_reflect_probes.value || !bump.surf_probe || !bump.probe_tex )
		return;

	i = s - WORLDMODEL->surfaces;
	if( i < 0 || i >= WORLDMODEL->numsurfaces )
		return;
	list = bump.surf_probe + i * PROBES_PER_SURF;
	if( list == bump.bound_list )
		return;
	bump.bound_list = list;

	for( n = 0; n < PROBES_PER_SURF && list[n]; n++ )
	{
		int p = list[n] - 1;

		VectorCopy( bump.probe_org[p], po[n] );
		po[n][3] = bump.probe_avg[p];
		pc[n][0] = p % PROBE_COLS;
		pc[n][1] = p / PROBE_COLS;
	}
	if( n )
	{
		pglUniform4fvARB( bump.u_po, n, (const float *)po );
		pglUniform2fvARB( bump.u_pc, n, (const float *)pc );
	}
	pglUniform1fARB( bump.u_nprobes, n ? n : -1.0f );
}

void R_BumpEnd( void )
{
	bump.current = NULL;
	if( !bump.active )
		return;
	bump.active = false;
	pglUseProgramObjectARB( 0 );
	GL_SelectTexture( XASH_TEXTURE3 );
	pglDisable( GL_TEXTURE_2D );
	glState.currentTextures[XASH_TEXTURE3] = -1;
	GL_SelectTexture( XASH_TEXTURE2 );
	pglDisable( GL_TEXTURE_2D );
	glState.currentTextures[XASH_TEXTURE2] = -1;
	GL_SelectTexture( XASH_TEXTURE0 );
}

/*
==============
R_BumpBrushSurface

brush entities draw their surfaces in any order: the shader follows the texture
==============
*/
void R_BumpBrushSurface( const msurface_t *s )
{
	texture_t *t = s->texinfo ? s->texinfo->texture : NULL;

	if( !R_BumpWanted() )
		return;
	if( t != bump.current )
	{
		R_BumpEnd();
		if( !t || R_BumpSkipTexture( t ) || FBitSet( s->flags, SURF_DRAWTURB|SURF_TRANSPARENT|SURF_DRAWSKY ))
		{
			bump.current = t;
			return;
		}
		if( !R_BumpBegin( t, R_BumpTextureIndex( t )))
			return;
	}
	R_BumpSurface( s );
}
