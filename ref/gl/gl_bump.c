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
CVAR_DEFINE_AUTO( r_reflect_dynamic_size, "128", FCVAR_ARCHIVE, "custom: pixels of each side of the reflections around the player (64, 128, 256)" );
CVAR_DEFINE_AUTO( r_reflect_planes, "2", FCVAR_ARCHIVE, "custom: flat faces (door, floor) reflected as a mirror, drawing the view again (0 to 2)" );
CVAR_DEFINE_AUTO( r_reflect_dynamic_sides, "2", FCVAR_ARCHIVE, "custom: sides of the reflections around the player drawn each frame (1 to 6)" );
CVAR_DEFINE_AUTO( r_reflect_probes, "0", FCVAR_ARCHIVE, "custom: fixed reflections of the map (probes taken when it loads) under the screen ones; Oscar: only the screen ones" );

// the shader is on for any of the three: relief, highlights, reflections
#define R_BumpWanted() ( r_bump.value || r_specular.value > 0.0f || r_reflect.value > 0.0f )

#define MAX_BUMP_TEXTURES	8192
#define BUMP_REFLECT		1
#define BUMP_RELIEF		2
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
	GLint		u_r0, u_r1, u_r2, u_nprobes, u_po, u_pc, u_skyavg, u_ssr;
	const model_t	*world;
	short		normalmap[MAX_BUMP_TEXTURES];	// -1 not looked for yet, 0 none
	float		gloss[MAX_BUMP_TEXTURES];	// average shine of the normal map
	int		sky0;				// skybox the colours were taken from
	char		*only;				// materials/maps/<map>.bsp/reflejos.txt: the textures that shine and reflect
	char		*relief;			// and relieve.txt: the ones with relief
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
	texture_t	*current;
	const short	*bound_list;			// probes of the last surface, in the uniforms
} bump;

// the view around the player
static struct
{
	GLuint		color, depth;	// 4 x 2 sides, like the probes
	GLuint		fbo, fcolor, fdepth;	// one side is drawn here (copying the depth of the window failed: zero, the reflections hit the surface itself)
	int		size, next;
	qboolean	drawing, valid;
	vec3_t		origin;
	float		zfar[6];	// each side its own: the engine sets the far clip from what each one sees
} dyn;

// mirrors: the planes of the faces that reflect, seen in the last frame; the nearest and
// biggest are drawn again from the other side of the plane (the eye reflected on it)
#define MAX_PLANES		2
#define PLANE_CANDIDATES	32
#define PLANE_MIN_AREA		12000.0f	// the door: two halves of 64 x 128

static struct
{
	vec4_t		cand[PLANE_CANDIDATES];		// normal, distance (in the world)
	float		weight[PLANE_CANDIDATES];	// how much of the view they take
	float		area[PLANE_CANDIDATES];		// of all their faces: small ones (the crates) keep the cube
	int		ncand;
	vec4_t		plane[MAX_PLANES];		// drawn this frame
	int		count;
	GLuint		fbo[MAX_PLANES], color[MAX_PLANES], depth[MAX_PLANES];
	int		w, h;
	vec3_t		eye[MAX_PLANES], vf[MAX_PLANES], vr[MAX_PLANES], vu[MAX_PLANES];
	float		tanx, tany;
	qboolean	drawing;
	int		current;
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
"uniform float bump, spec, refl, nprobes, skyavg, ssr;\n"
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
"}\n";

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
	bump.only = bump.relief = NULL;
	if( WORLDMODEL )
	{
		char path[256], mapa[64];

		COM_FileBase( WORLDMODEL->name, mapa, sizeof( mapa ));
		Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/reflejos.txt", mapa );
		bump.only = (char *)gEngfuncs.fsapi->LoadFile( path, NULL, false );
		Q_snprintf( path, sizeof( path ), "materials/maps/%s.bsp/relieve.txt", mapa );
		bump.relief = (char *)gEngfuncs.fsapi->LoadFile( path, NULL, false );
	}
	bump.sky0 = 0;
	R_BumpFreeProbes();
}

// a texture in a list: "name" or the start of the name, one per line, # comments
static qboolean R_BumpListed( const char *p, const char *name )
{
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
			return true;
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
	if( !bump.only || R_BumpListed( bump.only, t->name ))
		bump.flags[index] |= BUMP_REFLECT;
	if( bump.relief && R_BumpListed( bump.relief, t->name ))
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

	for( size = 64; size < r_reflect_dynamic_size.value && size < 256; size <<= 1 );
	if( size > gpGlobals->width || size > gpGlobals->height )
		return;
	if( !dyn.color || dyn.size != size )
	{
		if( dyn.color ) pglDeleteTextures( 1, &dyn.color );
		if( dyn.depth ) pglDeleteTextures( 1, &dyn.depth );
		if( dyn.fcolor ) pglDeleteTextures( 1, &dyn.fcolor );
		if( dyn.fdepth ) pglDeleteTextures( 1, &dyn.fdepth );
		if( dyn.fbo ) pglDeleteFramebuffers( 1, &dyn.fbo );
		dyn.color = R_BumpDynamicTexture( size * 4, size * 2, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR );
		dyn.depth = R_BumpDynamicTexture( size * 4, size * 2, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST );
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
	}
	if( !dyn.fbo )
		return;

	n = dyn.valid && VectorCompare( dyn.origin, rvp->vieworigin ) ? bound( 1, (int)r_reflect_dynamic_sides.value, 6 ) : 6;
	dyn.drawing = true;
	for( i = 0; i < n; i++ )
	{
		int j = dyn.next;

		dyn.next = ( dyn.next + 1 ) % 6;
		pglBindFramebuffer( GL_FRAMEBUFFER, dyn.fbo );
		R_DrawCubemapView( rvp->vieworigin, probe_angles[j], size );
		dyn.zfar[j] = Q_max( 256.0f, RI.farClip );
		pglBindFramebuffer( GL_FRAMEBUFFER, dyn.fbo );	// still the side: the copies read from it
		pglBindTexture( GL_TEXTURE_2D, dyn.color );
		pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, ( j % 4 ) * size, ( j / 4 ) * size, 0, 0, size, size );
		pglBindTexture( GL_TEXTURE_2D, dyn.depth );
		pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, ( j % 4 ) * size, ( j / 4 ) * size, 0, 0, size, size );
	}
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	dyn.drawing = false;
	glState.currentTextures[glState.activeTMU] = -1;
	glState.currentTexturesIndex[glState.activeTMU] = -1;
	VectorCopy( rvp->vieworigin, dyn.origin );
	dyn.valid = true;

	R_BumpPlanes( rvp );
}

/*
==============
R_BumpPlanes

the mirrors: the biggest planes seen in the last frame, drawn from the eye reflected on them,
cutting what is behind the plane
==============
*/
static void R_BumpPlanes( const ref_viewpass_t *rvp )
{
	int w = gpGlobals->width / 2, h = gpGlobals->height / 2;
	int i, j, k, want = bound( 0, (int)r_reflect_planes.value, MAX_PLANES );
	float tx, ty, fov;
	vec4_t chosen[MAX_PLANES];
	int nchosen = 0;

	// the ones already drawn keep their place unless another is clearly bigger (no jumping between two)
	for( i = 0; i < planar.ncand; i++ )
		for( j = 0; j < planar.count; j++ )
			if( DotProduct( planar.cand[i], planar.plane[j] ) > 0.999f && fabs( planar.cand[i][3] - planar.plane[j][3] ) < 1.0f )
				planar.weight[i] *= 1.5f;
	for( k = 0; k < want; k++ )
	{
		int best = -1;
		for( i = 0; i < planar.ncand; i++ )
			if( planar.weight[i] > 0.0f && planar.area[i] >= PLANE_MIN_AREA && ( best < 0 || planar.weight[i] > planar.weight[best] ))
				best = i;
		if( best < 0 )
			break;
		Vector4Copy( planar.cand[best], chosen[nchosen] );
		nchosen++;
		planar.weight[best] = 0.0f;
	}
	planar.ncand = 0;
	planar.count = 0;
	if( !nchosen || w < 16 || h < 16 )
		return;

	if( !planar.fbo[0] || planar.w != w || planar.h != h )
	{
		for( i = 0; i < MAX_PLANES; i++ )
		{
			if( planar.color[i] ) pglDeleteTextures( 1, &planar.color[i] );
			if( planar.depth[i] ) pglDeleteTextures( 1, &planar.depth[i] );
			if( planar.fbo[i] ) pglDeleteFramebuffers( 1, &planar.fbo[i] );
			planar.color[i] = R_BumpDynamicTexture( w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR );
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
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glState.currentTextures[glState.activeTMU] = -1;
		glState.currentTexturesIndex[glState.activeTMU] = -1;
		planar.w = w;
		planar.h = h;
	}

	// a little wider than the view: the reflected view may be turned half a turn, it covers the same
	fov = Q_min( rvp->fov_x + 6.0f, 170.0f );
	tx = tan( fov * M_PI_F / 360.0f );
	ty = tan( rvp->fov_y * M_PI_F / 360.0f ) * tx / tan( rvp->fov_x * M_PI_F / 360.0f );

	planar.drawing = true;
	for( k = 0; k < nchosen; k++ )
	{
		ref_viewpass_t v;
		vec3_t f, r, u;
		float hgt;

		if( !planar.fbo[k] )
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

		Vector4Copy( chosen[k], planar.plane[planar.count] );
		planar.current = planar.count;
		pglBindFramebuffer( GL_FRAMEBUFFER, planar.fbo[k] );
		RI.viewleaf = NULL;
		R_RenderFrame( &v );
		RI.viewleaf = NULL;		// force markleafs next frame
		pglDisable( GL_CLIP_PLANE0 );

		// how it was seen, for gl_post.c
		VectorCopy( v.vieworigin, planar.eye[planar.count] );
		VectorCopy( RI.vforward, planar.vf[planar.count] );
		VectorCopy( RI.vright, planar.vr[planar.count] );
		VectorCopy( RI.vup, planar.vu[planar.count] );
		planar.count++;
	}
	planar.drawing = false;
	planar.tanx = tx;
	planar.tany = ty;
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glState.currentTextures[glState.activeTMU] = -1;
	glState.currentTexturesIndex[glState.activeTMU] = -1;
}

// R_SetupGL, with the view matrix loaded: in a mirror, cut what is behind its plane
void R_BumpClipPlane( void )
{
	if( planar.drawing )
	{
		const float *p = planar.plane[planar.current];
		GLdouble eq[4] = { p[0], p[1], p[2], -p[3] - 0.5 };

		pglClipPlane( GL_CLIP_PLANE0, eq );
		pglEnable( GL_CLIP_PLANE0 );
	}
	else pglDisable( GL_CLIP_PLANE0 );
}

qboolean R_BumpDrawingPlanar( void )
{
	return planar.drawing;
}

// for gl_post.c: the mirrors of this frame
int R_BumpMirrors( GLuint *tex, vec4_t *plane, vec3_t *eye, vec3_t *vf, vec3_t *vr, vec3_t *vu, float *tanx, float *tany )
{
	int i;

	if( !r_reflect.value || !r_reflect_dynamic.value )
		return 0;
	for( i = 0; i < planar.count; i++ )
	{
		tex[i] = planar.color[i];
		Vector4Copy( planar.plane[i], plane[i] );
		VectorCopy( planar.eye[i], eye[i] );
		VectorCopy( planar.vf[i], vf[i] );
		VectorCopy( planar.vr[i], vr[i] );
		VectorCopy( planar.vu[i], vu[i] );
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
	*color = dyn.color;
	*depth = dyn.depth;
	VectorCopy( dyn.origin, origin );
	memcpy( zfar, dyn.zfar, sizeof( dyn.zfar ));
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
		pglUniform1fARB( bump.u_spec, FBitSet( f, BUMP_REFLECT ) ? r_specular.value : 0.0f );
		pglUniform1fARB( bump.u_refl, FBitSet( f, BUMP_REFLECT ) ? r_reflect.value : 0.0f );
		bump.reflects = FBitSet( f, BUMP_REFLECT ) && r_reflect.value;
	}
	pglUniform1fARB( bump.u_ssr, R_PostScreenReflections( ) ? 1.0f : 0.0f );
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
static void R_BumpPlaneSeen( const msurface_t *s, const vec3_t N )
{
	vec3_t c, size;
	float d, h, area, w;
	int i;

	if( FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ) || !R_PostScreenReflections( ) || !s->info )
		return;

	d = FBitSet( s->flags, SURF_PLANEBACK ) ? -s->plane->dist : s->plane->dist;
	VectorCopy( s->info->origin, c );
	if( RI.currententity && RI.currententity != CL_GetEntityByIndex( 0 ))
	{
		vec3_t t;
		Matrix4x4_VectorTransform( RI.objectMatrix, s->info->origin, c );
		VectorSet( t, RI.objectMatrix[0][3], RI.objectMatrix[1][3], RI.objectMatrix[2][3] );
		d += DotProduct( N, t );
	}
	h = DotProduct( N, RI.rvp.vieworigin ) - d;
	if( h < 1.0f )
		return;

	// the size of the face over its distance squared: the part of the view it takes
	VectorSubtract( s->info->maxs, s->info->mins, size );
	area = Q_max( size[0] * size[1], Q_max( size[0] * size[2], size[1] * size[2] ));
	VectorSubtract( c, RI.rvp.vieworigin, c );
	w = area / ( VectorLength( c ) + 128.0f );

	for( i = 0; i < planar.ncand; i++ )
	{
		if( DotProduct( planar.cand[i], N ) > 0.999f && fabs( planar.cand[i][3] - d ) < 1.0f )
		{
			planar.weight[i] += w;
			planar.area[i] += area;
			return;
		}
	}
	if( planar.ncand >= PLANE_CANDIDATES )
		return;
	VectorCopy( N, planar.cand[i] );
	planar.cand[i][3] = d;
	planar.weight[i] = w;
	planar.area[i] = area;
	planar.ncand++;
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

	if( bump.reflects )
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
