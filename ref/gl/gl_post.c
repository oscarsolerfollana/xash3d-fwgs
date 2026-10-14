/*
gl_post.c - post processing with shaders: ambient occlusion, eye adaptation,
bloom, filmic tone mapping (custom)
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
The 3D view (world, entities, view model) is drawn into an offscreen
framebuffer with a colour texture (16 bit float when the card has it) and a
depth texture, instead of straight to the screen. Then, before the HUD:
  1. ambient occlusion (r_ssao): from the depth, at half size, each pixel
     looks at points around it on its surface and darkens where nearby
     geometry covers them (corners, under ledges, where walls meet the
     floor); then blurred keeping edges
  1b. reflections of what is on the screen (r_ssr): the glossy surfaces
     (gl_bump.c) leave in the alpha how much they reflect; from each of
     those pixels a ray goes the way the reflection does, in steps, until it
     passes behind what the depth buffer has there, and takes the colour of
     that point (a lamp, the ceiling, a player): it moves with the view and
     with what moves. What is not on the screen is not reflected (the probes
     of gl_bump.c are still under it). At half size and blurred a bit
  1c. reflections around the player (r_reflect_dynamic, gl_bump.c draws the
     six sides around the eye): from the pixel, the reflected ray is followed
     with the depth of those sides (a distance impostor: the point of the ray
     at the distance the sides see in its direction, a few times) and the
     colour there is taken. It depends on where the player is, not on where
     he looks. Instead of 1b when there are sides
  2. eye adaptation (r_hdr): the average brightness of the view (log,
     shrunk to one pixel) sets the exposure, which moves towards it over
     r_hdr_speed: coming out of a tunnel the outside is too bright for a
     moment, going in it is too dark
  3. bloom (r_hdr_bloom): what is brighter than r_hdr_bloom_threshold after
     the exposure glows, blurred at a quarter of the size
  4. the view on the screen: occlusion, exposure, bloom, a filmic curve
     (ACES) instead of cutting at white, saturation, a bit of sharpening and
     vignette
Half-Life lighting is baked in 0..255 lightmaps and the fixed function
pipeline clamps what it writes, so this is not HDR lighting; the exposure
and the curve give its look. With r_post 0, or when the card lacks shaders
or framebuffers, nothing of this runs (gl_bloom.c still works then).
*/

CVAR_DEFINE_AUTO( r_post, "1", FCVAR_ARCHIVE, "custom: shader post processing (ambient occlusion, eye adaptation, bloom, filmic tone mapping)" );
CVAR_DEFINE_AUTO( r_ssao, "1", FCVAR_ARCHIVE, "custom: screen space ambient occlusion" );
CVAR_DEFINE_AUTO( r_ssao_radius, "16", FCVAR_ARCHIVE, "custom: ambient occlusion radius in world units" );
CVAR_DEFINE_AUTO( r_ssao_strength, "0.6", FCVAR_ARCHIVE, "custom: ambient occlusion strength" );
CVAR_DEFINE_AUTO( r_hdr, "1", FCVAR_ARCHIVE, "custom: eye adaptation and filmic tone mapping" );
CVAR_DEFINE_AUTO( r_hdr_exposure, "1", FCVAR_ARCHIVE, "custom: exposure multiplier" );
CVAR_DEFINE_AUTO( r_hdr_key, "0.22", FCVAR_ARCHIVE, "custom: average brightness the eye adapts to" );
CVAR_DEFINE_AUTO( r_hdr_min, "0.6", FCVAR_ARCHIVE, "custom: lowest automatic exposure (bright places)" );
CVAR_DEFINE_AUTO( r_hdr_max, "1.5", FCVAR_ARCHIVE, "custom: highest automatic exposure (dark places)" );
CVAR_DEFINE_AUTO( r_hdr_speed, "1.5", FCVAR_ARCHIVE, "custom: eye adaptation speed" );
CVAR_DEFINE_AUTO( r_hdr_bloom, "0.6", FCVAR_ARCHIVE, "custom: bloom strength" );
CVAR_DEFINE_AUTO( r_hdr_bloom_threshold, "0.75", FCVAR_ARCHIVE, "custom: brightness (after exposure) above which things glow" );
CVAR_DEFINE_AUTO( r_post_saturation, "1.05", FCVAR_ARCHIVE, "custom: colour saturation" );
CVAR_DEFINE_AUTO( r_post_sharpen, "0.25", FCVAR_ARCHIVE, "custom: sharpening" );
CVAR_DEFINE_AUTO( r_post_vignette, "0.25", FCVAR_ARCHIVE, "custom: darker corners" );
CVAR_DEFINE_AUTO( r_ssr, "0", FCVAR_ARCHIVE, "custom: reflections of what is on the screen on glossy surfaces" );
CVAR_DEFINE_AUTO( r_ssr_strength, "1", FCVAR_ARCHIVE, "custom: strength of the screen reflections" );
CVAR_DEFINE_AUTO( r_post_debug, "0", 0, "custom: 1 shows the ambient occlusion, 2 the bloom, 3 the screen reflections" );

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER			0x8D40
#define GL_COLOR_ATTACHMENT0		0x8CE0
#define GL_DEPTH_ATTACHMENT		0x8D00
#define GL_FRAMEBUFFER_COMPLETE		0x8CD5
#endif
#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB			0x881A
#endif
#ifndef GL_HALF_FLOAT_ARB
#define GL_HALF_FLOAT_ARB		0x140B
#endif
#ifndef GL_FRAGMENT_SHADER_ARB
#define GL_FRAGMENT_SHADER_ARB		0x8B30
#define GL_VERTEX_SHADER_ARB		0x8B31
#define GL_OBJECT_COMPILE_STATUS_ARB	0x8B81
#define GL_OBJECT_LINK_STATUS_ARB	0x8B82
#endif

typedef struct
{
	GLuint	fbo, tex;
	int	w, h;
} rtarget_t;

enum { RT_SCENE, RT_AO, RT_AO2, RT_B1, RT_B2, RT_L64, RT_L16, RT_L4, RT_L1, RT_SSR, RT_SSR2, RT_COUNT };

static struct
{
	qboolean	tried, ok;
	int		w, h;
	GLuint		depth;
	rtarget_t	rt[RT_COUNT];
	GLhandleARB	p_ssao, p_aoblur, p_ssr, p_cube, p_bright, p_blur, p_lum, p_lumdown, p_final;
	vec3_t		eye, vf, vr, vu;	// of the scene, for the reflections around the player
	float		exposure;
	double		lasttime;
	float		tanx, tany;	// of the scene: the view model changes the fov before R_PostEnd
	qboolean	active;		// this frame goes through the framebuffer
} post;

static const char *vs_quad =
"void main( void ) { gl_TexCoord[0] = gl_MultiTexCoord0; gl_Position = ftransform(); }\n";

// view space position from the depth buffer, at the centre of the depth pixel that is read (the
// occlusion runs at half size: reconstructing at the exact point while reading the next pixel tilted
// ceilings and walls on one side, and they darkened themselves in bands)
// centro(): the depth texel of a uv. The pixel centres of the half size passes fall right on the edge
// of two depth texels and floor() took one or the other by rounding, differently on each half of the
// screen (stripes above the crosshair and to its right): a quarter texel more keeps it off the edge
#define FS_COMMON \
"uniform sampler2D depthmap;\n" \
"uniform float znear, zfar;\n" \
"uniform vec2 tanfov;\n" \
"float lineal( float d ) { float z = d * 2.0 - 1.0; return 2.0 * znear * zfar / ( zfar + znear - z * ( zfar - znear )); }\n" \
"uniform vec2 dtexel;\n" \
"vec2 centro( vec2 uv ) { return ( floor( uv / dtexel + 0.25 ) + 0.5 ) * dtexel; }\n" \
"vec3 posicion( vec2 uv ) { uv = centro( uv ); float z = lineal( texture2D( depthmap, uv ).r ); return vec3(( uv * 2.0 - 1.0 ) * tanfov * z, -z ); }\n"

// Alchemy AO (McGuire et al. 2011): only what rises above the plane of the pixel occludes it, so a flat
// floor never darkens itself (with the depth compared along the view, at grazing angles it did: bands)
static const char *fs_ssao =
FS_COMMON
"uniform float radius, strength;\n"
"uniform vec2 texel;\n"
"vec3 lado( vec3 p, vec3 a, vec3 b ) { return abs( a.z - p.z ) < abs( b.z - p.z ) ? a - p : p - b; }\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	float d = texture2D( depthmap, centro( uv )).r;\n"
"	if( d >= 0.9999 || d < 0.3 ) { gl_FragColor = vec4( 1.0 ); return; }\n"
"	vec3 p = posicion( uv );\n"
"	float fade = 1.0 - smoothstep( 800.0, 2000.0, -p.z );\n"
"	if( fade <= 0.0 ) { gl_FragColor = vec4( 1.0 ); return; }\n"
	// a thin slit (where the two halves of a door meet): both sides in front of it, no shadow inside
"	for( int k = 1; k <= 4; k++ )\n"
"	{\n"
"		vec2 ox = vec2( texel.x * float( k ), 0.0 ), oy = vec2( 0.0, texel.y * float( k ));\n"
"		if( min( posicion( uv + ox ).z, posicion( uv - ox ).z ) > p.z + 4.0 || min( posicion( uv + oy ).z, posicion( uv - oy ).z ) > p.z + 4.0 )\n"
"		{ gl_FragColor = vec4( 1.0 ); return; }\n"
"	}\n"
	// normal from the neighbour on the same surface (the closer in depth), so edges do not bend it
"	vec3 dx = lado( p, posicion( uv + vec2( texel.x, 0.0 )), posicion( uv - vec2( texel.x, 0.0 )));\n"
"	vec3 dy = lado( p, posicion( uv + vec2( 0.0, texel.y )), posicion( uv - vec2( 0.0, texel.y )));\n"
"	vec3 n = normalize( cross( dx, dy ));\n"
"	if( dot( n, p ) > 0.0 ) n = -n;\n"
"	float rad = min( radius, -p.z * tanfov.y * 0.12 );\n"
"	vec2 pr = vec2( rad ) / ( -p.z * tanfov ) * 0.5;\n"	// radius on the screen (uv)
"	float ang = fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 )))) * 6.2831853;\n"	// interleaved gradient noise: the sin() hash made bands
"	float occ = 0.0;\n"
"	for( int i = 0; i < 16; i++ )\n"
"	{\n"
"		float fi = float( i );\n"
"		float a = ang + fi * 2.39996;\n"
"		vec2 quv = uv + vec2( cos( a ), sin( a )) * sqrt(( fi + 0.5 ) / 16.0 ) * pr;\n"
"		if( quv.x < 0.0 || quv.x > 1.0 || quv.y < 0.0 || quv.y > 1.0 ) continue;\n"
"		float dq = texture2D( depthmap, centro( quv )).r;\n"
"		if( dq < 0.3 || dq >= 0.9999 ) continue;\n"
"		vec3 v = posicion( quv ) - p;\n"
"		float vv = dot( v, v );\n"
"		float caida = max( 1.0 - vv / ( rad * rad * 4.0 ), 0.0 );\n"	// far ones do not count
"		occ += max( dot( v, n ) - 0.02 * -p.z, 0.0 ) / ( vv + 0.01 * rad * rad ) * caida * rad;\n"
"	}\n"
"	gl_FragColor = vec4( vec3( clamp( 1.0 - occ / 16.0 * strength * 2.0 * fade, 0.0, 1.0 )), 1.0 );\n"
"}\n";

// blur of the occlusion that does not cross depth edges
static const char *fs_aoblur =
FS_COMMON
"uniform sampler2D aomap;\n"
"uniform vec2 texel;\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	float z0 = lineal( texture2D( depthmap, centro( uv )).r );\n"
"	float suma = 0.0, peso = 0.0;\n"
"	for( int x = -3; x <= 3; x++ )\n"
"	for( int y = -3; y <= 3; y++ )\n"
"	{\n"
"		vec2 o = uv + vec2( float( x ), float( y )) * texel;\n"
"		float z = lineal( texture2D( depthmap, centro( o )).r );\n"
"		float w = 1.0 / ( 1.0 + abs( z - z0 ) * 0.5 );\n"
"		suma += texture2D( aomap, o ).r * w; peso += w;\n"
"	}\n"
"	gl_FragColor = vec4( vec3( suma / peso ), 1.0 );\n"
"}\n";

// reflections of the screen: a ray from the pixel the way the reflection goes, in steps growing with
// the distance, until it is behind what the depth buffer has there (and not far behind: that is
// passing behind something), then halved down to the surface. The colour goes premultiplied by how
// much it shows (the blur and the half size mix it with the pixels that do not reflect)
static const char *fs_ssr =
FS_COMMON
"uniform sampler2D scene;\n"
"uniform vec2 texel;\n"
"uniform float strength;\n"
"vec3 lado( vec3 p, vec3 a, vec3 b ) { return abs( a.z - p.z ) < abs( b.z - p.z ) ? a - p : p - b; }\n"
"vec2 pantalla( vec3 q ) { return q.xy / ( -q.z * tanfov ) * 0.5 + 0.5; }\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	float m = clamp( 1.0 - texture2D( scene, uv ).a, 0.0, 1.0 );\n"
"	float d = texture2D( depthmap, centro( uv )).r;\n"
"	gl_FragColor = vec4( 0.0 );\n"
"	if( m < 0.01 || d < 0.3 || d >= 0.9999 ) return;\n"
"	vec3 p = posicion( uv );\n"
"	vec3 dx = lado( p, posicion( uv + vec2( texel.x, 0.0 )), posicion( uv - vec2( texel.x, 0.0 )));\n"
"	vec3 dy = lado( p, posicion( uv + vec2( 0.0, texel.y )), posicion( uv - vec2( 0.0, texel.y )));\n"
"	vec3 n = normalize( cross( dx, dy ));\n"
"	if( dot( n, p ) > 0.0 ) n = -n;\n"
"	vec3 r = reflect( normalize( p ), n );\n"
"	float alojo = 1.0 - smoothstep( 0.8, 0.98, r.z );\n"	// towards the camera it still hits what is in front (a lamp over a door seen head on); straight back, only the view model
"	if( alojo <= 0.0 ) return;\n"
"	float paso = max( 2.0, -p.z * 0.015 );\n"
"	float ruido = fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ))));\n"
"	vec3 q = p + n * 0.5 + r * paso * ruido;\n"
"	vec3 antes = q;\n"
"	vec2 hit = vec2( -1.0 );\n"
"	for( int i = 0; i < 48; i++ )\n"
"	{\n"
"		antes = q;\n"
"		q += r * paso;\n"
"		paso *= 1.07;\n"
"		if( q.z > -4.0 ) break;\n"
"		vec2 sq = pantalla( q );\n"
"		if( sq.x < 0.0 || sq.x > 1.0 || sq.y < 0.0 || sq.y > 1.0 ) break;\n"
"		float ds = texture2D( depthmap, sq ).r;\n"
"		if( ds < 0.3 ) continue;\n"	// the view model: what is behind it is not known
"		float detras = -q.z - lineal( ds );\n"
"		if( detras <= 0.0 || detras > paso * 1.5 + 4.0 ) continue;\n"
"		for( int j = 0; j < 6; j++ )\n"
"		{\n"
"			vec3 mid = ( antes + q ) * 0.5;\n"
"			if( -mid.z - lineal( texture2D( depthmap, pantalla( mid )).r ) > 0.0 ) q = mid; else antes = mid;\n"
"		}\n"
"		hit = pantalla( q );\n"
"		break;\n"
"	}\n"
"	if( hit.x < 0.0 ) return;\n"
"	vec2 borde = min( hit, 1.0 - hit );\n"
"	float f = smoothstep( 0.0, 0.08, min( borde.x, borde.y )) * alojo * m * strength;\n"
"	gl_FragColor = vec4( texture2D( scene, hit ).rgb * f, f );\n"
"}\n";

// reflections around the player: the pixel and its normal in the world, the reflected ray, and the
// point of the ray that the sides around the eye see at that distance, a few times over (the first
// guess, the ray from the eye, is right far away; each step corrects for the pixel not being at the eye)
static const char *fs_cube =
FS_COMMON
"uniform sampler2D scene, cubecolor, cubedepth;\n"
"uniform vec2 texel;\n"
"uniform vec3 eye, vf, vr, vu, corg;\n"
"uniform float strength, csize;\n"
"uniform float cfar[6];\n"
"uniform sampler2D plano0, plano1;\n"	// the mirrors (gl_bump.c R_BumpPlanes)
"uniform vec4 pn[2];\n"
"uniform vec3 pe[2], pf[2], pr[2], pu[2];\n"
"uniform vec2 ptan;\n"
"uniform float nplanos;\n"
// where the mirror view saw this point of its plane
"vec2 espejo( vec3 P, int k ) { vec3 v = P - pe[k]; return vec2( dot( v, pr[k] ), dot( v, pu[k] )) / ( dot( v, pf[k] ) * ptan ) * 0.5 + 0.5; }\n"
"bool enplano( vec3 P, vec3 N, int k ) { return dot( N, pn[k].xyz ) > 0.8 && abs( dot( P, pn[k].xyz ) - pn[k].w ) < 2.0 + 0.004 * length( P - eye ); }\n"
"vec3 lado( vec3 p, vec3 a, vec3 b ) { return abs( a.z - p.z ) < abs( b.z - p.z ) ? a - p : p - b; }\n"
// the cell of a direction, as gl_bump.c draws them (right = forward x up, bottom row first); m: how much
// of the direction goes along that side, to turn the depth (along the side) into a distance
"vec2 cara( vec3 R, out float m, out float cell )\n"
"{\n"
"	vec3 a = abs( R ), f, u = vec3( 0.0, 0.0, 1.0 );\n"
"	if( a.x >= a.y && a.x >= a.z ) { f = vec3( sign( R.x ), 0.0, 0.0 ); cell = R.x > 0.0 ? 0.0 : 1.0; }\n"
"	else if( a.y >= a.z ) { f = vec3( 0.0, sign( R.y ), 0.0 ); cell = R.y > 0.0 ? 2.0 : 3.0; }\n"
"	else if( R.z > 0.0 ) { f = vec3( 0.0, 0.0, 1.0 ); u = vec3( -1.0, 0.0, 0.0 ); cell = 4.0; }\n"
"	else { f = vec3( 0.0, 0.0, -1.0 ); u = vec3( 1.0, 0.0, 0.0 ); cell = 5.0; }\n"
"	m = dot( R, f );\n"
"	vec2 c = vec2( dot( R, cross( f, u )), dot( R, u )) / m * 0.5 + 0.5;\n"
"	vec2 cc = vec2( mod( cell, 4.0 ), floor( cell / 4.0 ));\n"
"	return ( cc + ( 0.5 + clamp( c, 0.0, 1.0 ) * ( csize - 1.0 )) / csize ) * vec2( 0.25, 0.5 );\n"
"}\n"
"float distancia( vec3 dir )\n"
"{\n"
"	float m, cell;\n"
"	vec2 c = cara( dir, m, cell );\n"
"	float z = texture2D( cubedepth, c ).r * 2.0 - 1.0;\n"
"	float fr = cfar[int( cell + 0.5 )];\n"
"	return 2.0 * 4.0 * fr / ( fr + 4.0 - z * ( fr - 4.0 )) / m;\n"
"}\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	float m = clamp( 1.0 - texture2D( scene, uv ).a, 0.0, 1.0 );\n"
"	float d = texture2D( depthmap, centro( uv )).r;\n"
"	gl_FragColor = vec4( 0.0 );\n"
"	if( m < 0.01 || d < 0.3 || d >= 0.9999 ) return;\n"
"	vec3 p = posicion( uv );\n"
"	vec3 dx = lado( p, posicion( uv + vec2( texel.x, 0.0 )), posicion( uv - vec2( texel.x, 0.0 )));\n"
"	vec3 dy = lado( p, posicion( uv + vec2( 0.0, texel.y )), posicion( uv - vec2( 0.0, texel.y )));\n"
"	vec3 n = normalize( cross( dx, dy ));\n"
"	if( dot( n, p ) > 0.0 ) n = -n;\n"
"	vec3 P = eye + vr * p.x + vu * p.y - vf * p.z;\n"
"	vec3 N = normalize( vr * n.x + vu * n.y - vf * n.z );\n"
"	float f = m * strength;\n"
"	if( nplanos > 0.5 && enplano( P, N, 0 )) { gl_FragColor = vec4( texture2D( plano0, espejo( P, 0 )).rgb * f, f ); return; }\n"
"	if( nplanos > 1.5 && enplano( P, N, 1 )) { gl_FragColor = vec4( texture2D( plano1, espejo( P, 1 )).rgb * f, f ); return; }\n"
"	vec3 R = reflect( normalize( P - eye ), N );\n"
	// in steps from the surface outwards, until the ray is behind what the eye sees in that direction
	// (not far behind: that is passing behind something), then halved down; a guess that jumped to
	// the answer reflected the door on itself, and from one frame to the next took one answer or another
"	vec3 o = P - corg + N * 1.0;\n"
"	float s = 0.0, paso = 4.0, antes = 0.0;\n"
"	vec3 X = o + R * 8192.0;\n"
"	for( int i = 0; i < 40; i++ )\n"
"	{\n"
"		antes = s;\n"
"		s += paso;\n"
"		paso *= 1.15;\n"
"		vec3 q = o + R * s;\n"
"		float lq = length( q );\n"
"		float detras = lq - distancia( q / lq );\n"
"		if( detras <= 0.0 || detras > paso * 2.0 + 16.0 ) continue;\n"
"		for( int j = 0; j < 6; j++ )\n"
"		{\n"
"			float mid = ( antes + s ) * 0.5;\n"
"			vec3 qm = o + R * mid;\n"
"			float lm = length( qm );\n"
"			if( lm - distancia( qm / lm ) > 0.0 ) s = mid; else antes = mid;\n"
"		}\n"
"		X = o + R * s;\n"
"		break;\n"
"	}\n"
"	float mm, cl;\n"
"	vec3 c = texture2D( cubecolor, cara( normalize( X ), mm, cl )).rgb;\n"
"	gl_FragColor = vec4( c * f, f );\n"
"}\n";

// the colours of the 0..255 view are taken as gamma 2.2 and worked on in linear
static const char *fs_lum =
"uniform sampler2D scene;\n"
"uniform vec2 texel;\n"
"void main( void )\n"
"{\n"
"	float s = 0.0;\n"
"	for( int x = -2; x <= 1; x++ )\n"
"	for( int y = -2; y <= 1; y++ )\n"
"	{\n"
"		vec3 c = pow( texture2D( scene, gl_TexCoord[0].xy + ( vec2( float( x ), float( y )) + 0.5 ) * texel ).rgb, vec3( 2.2 ));\n"
"		s += log( dot( c, vec3( 0.2126, 0.7152, 0.0722 )) + 0.001 );\n"
"	}\n"
"	gl_FragColor = vec4( s / 16.0 );\n"
"}\n";

static const char *fs_lumdown =
"uniform sampler2D src;\n"
"uniform vec2 texel;\n"
"void main( void )\n"
"{\n"
"	float s = 0.0;\n"
"	for( int x = -2; x <= 1; x++ )\n"
"	for( int y = -2; y <= 1; y++ )\n"
"		s += texture2D( src, gl_TexCoord[0].xy + ( vec2( float( x ), float( y )) + 0.5 ) * texel ).r;\n"
"	gl_FragColor = vec4( s / 16.0 );\n"
"}\n";

static const char *fs_bright =
"uniform sampler2D scene;\n"
"uniform vec2 texel;\n"
"uniform float exposure, threshold;\n"
"void main( void )\n"
"{\n"
"	vec3 c = vec3( 0.0 );\n"
"	for( int x = -1; x <= 1; x++ )\n"
"	for( int y = -1; y <= 1; y++ )\n"
"		c += pow( texture2D( scene, gl_TexCoord[0].xy + vec2( float( x ), float( y )) * texel ).rgb, vec3( 2.2 ));\n"
"	c = c / 9.0 * exposure;\n"
"	float l = dot( c, vec3( 0.2126, 0.7152, 0.0722 ));\n"
"	gl_FragColor = vec4( c * ( max( l - threshold, 0.0 ) / max( l, 0.0001 )), 1.0 );\n"
"}\n";

static const char *fs_blur =
"uniform sampler2D src;\n"
"uniform vec2 dir;\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	vec4 c = texture2D( src, uv ) * 0.2270270270;\n"
"	c += ( texture2D( src, uv + dir * 1.3846153846 ) + texture2D( src, uv - dir * 1.3846153846 )) * 0.3162162162;\n"
"	c += ( texture2D( src, uv + dir * 3.2307692308 ) + texture2D( src, uv - dir * 3.2307692308 )) * 0.0702702703;\n"
"	gl_FragColor = c;\n"
"}\n";

static const char *fs_final =
"uniform sampler2D scene, aomap, bloom, ssrmap;\n"
"uniform vec2 texel;\n"
"uniform float exposure, bloomk, aok, ssrk, sat, sharpen, vignette, hdr;\n"
"uniform int debug;\n"
"vec3 aces( vec3 x ) { return clamp(( x * ( 2.51 * x + 0.03 )) / ( x * ( 2.43 * x + 0.59 ) + 0.14 ), 0.0, 1.0 ); }\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	vec3 c = texture2D( scene, uv ).rgb;\n"
"	if( sharpen > 0.0 )\n"
"	{\n"
"		vec3 v = texture2D( scene, uv + vec2( texel.x, 0.0 )).rgb + texture2D( scene, uv - vec2( texel.x, 0.0 )).rgb\n"
"			+ texture2D( scene, uv + vec2( 0.0, texel.y )).rgb + texture2D( scene, uv - vec2( 0.0, texel.y )).rgb;\n"
"		c = clamp( c + ( c - v * 0.25 ) * sharpen, 0.0, 1.0 );\n"
"	}\n"
"	vec4 rf = texture2D( ssrmap, uv ) * ssrk;\n"
"	c = c * ( 1.0 - rf.a ) + rf.rgb;\n"
"	float ao = mix( 1.0, texture2D( aomap, uv ).r, aok );\n"
"	vec3 b = texture2D( bloom, uv ).rgb * bloomk;\n"
"	if( debug == 1 ) { gl_FragColor = vec4( vec3( ao ), 1.0 ); return; }\n"
"	if( debug == 2 ) { gl_FragColor = vec4( pow( b, vec3( 1.0 / 2.2 )), 1.0 ); return; }\n"
"	if( debug == 3 ) { gl_FragColor = vec4( rf.rgb + vec3( 0.0, 0.0, 0.25 * rf.a ), 1.0 ); return; }\n"
"	vec3 lin = pow( c, vec3( 2.2 )) * ao;\n"
"	if( hdr > 0.5 )\n"
"	{\n"
"		lin = lin * exposure + b;\n"
"		lin = aces( lin * 1.5 ) / aces( vec3( 1.5 * 1.15 ));\n"	// a white at 1.15 stays white
"	}\n"
"	else lin += b;\n"
"	float l = dot( lin, vec3( 0.2126, 0.7152, 0.0722 ));\n"
"	lin = max( mix( vec3( l ), lin, sat ), 0.0 );\n"
"	vec2 d = uv - 0.5;\n"
"	lin *= 1.0 - vignette * dot( d, d ) * 1.6;\n"
"	gl_FragColor = vec4( pow( clamp( lin, 0.0, 1.0 ), vec3( 1.0 / 2.2 )), 1.0 );\n"
"}\n";

static void *R_PostProc( const char *name )
{
	char ext[64];
	void *f = gEngfuncs.GL_GetProcAddress( name );

	if( !f )
	{
		Q_snprintf( ext, sizeof( ext ), "%sEXT", name );
		f = gEngfuncs.GL_GetProcAddress( ext );
	}
	return f;
}

static GLhandleARB R_PostShader( const char *nombre, const char *fs )
{
	GLhandleARB v, f, p;
	GLint ok = 0;
	char log[2048];

	v = pglCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	pglShaderSourceARB( v, 1, &vs_quad, NULL );
	pglCompileShaderARB( v );
	f = pglCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	pglShaderSourceARB( f, 1, &fs, NULL );
	pglCompileShaderARB( f );
	pglGetObjectParameterivARB( f, GL_OBJECT_COMPILE_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( f, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_post: shader %s: %s\n", nombre, log );
		return 0;
	}
	p = pglCreateProgramObjectARB();
	pglAttachObjectARB( p, v );
	pglAttachObjectARB( p, f );
	pglLinkProgramARB( p );
	pglGetObjectParameterivARB( p, GL_OBJECT_LINK_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( p, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_post: program %s: %s\n", nombre, log );
		return 0;
	}
	return p;
}

static void R_PostUniforms1i( GLhandleARB p, const char *n, int v ) { GLint l = pglGetUniformLocationARB( p, n ); if( l >= 0 ) pglUniform1iARB( l, v ); }
static void R_PostUniform1f( GLhandleARB p, const char *n, float v ) { GLint l = pglGetUniformLocationARB( p, n ); if( l >= 0 ) pglUniform1fARB( l, v ); }
static void R_PostUniform3f( GLhandleARB p, const char *n, const vec3_t v ) { GLint l = pglGetUniformLocationARB( p, n ); if( l >= 0 ) pglUniform3fARB( l, v[0], v[1], v[2] ); }
static void R_PostUniform2f( GLhandleARB p, const char *n, float a, float b ) { GLint l = pglGetUniformLocationARB( p, n ); if( l >= 0 ) pglUniform2fARB( l, a, b ); }

static GLuint R_PostTexture( int w, int h, GLenum ifmt, GLenum fmt, GLenum type, GLenum filter )
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

static qboolean R_PostTarget( rtarget_t *rt, int w, int h, GLenum ifmt, GLenum type, GLuint depth )
{
	rt->w = w; rt->h = h;
	rt->tex = R_PostTexture( w, h, ifmt, GL_RGBA, type, GL_LINEAR );
	pglGenFramebuffers( 1, &rt->fbo );
	pglBindFramebuffer( GL_FRAMEBUFFER, rt->fbo );
	pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt->tex, 0 );
	if( depth )
		pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0 );
	return pglCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
}

static void R_PostFree( void )
{
	for( int i = 0; i < RT_COUNT; i++ )
	{
		if( post.rt[i].fbo ) pglDeleteFramebuffers( 1, &post.rt[i].fbo );
		if( post.rt[i].tex ) pglDeleteTextures( 1, &post.rt[i].tex );
		post.rt[i].fbo = post.rt[i].tex = 0;
	}
	if( post.depth ) pglDeleteTextures( 1, &post.depth );
	post.depth = 0;
	post.w = post.h = 0;
}

static void R_PostRestoreState( void )
{
	// the renderer keeps which texture is bound on each unit: it no longer knows
	for( int i = 0; i < MAX_TEXTURE_UNITS; i++ )
	{
		glState.currentTextures[i] = -1;
		glState.currentTexturesIndex[i] = -1;
	}
	pglUseProgramObjectARB( 0 );
	GL_SelectTexture( XASH_TEXTURE0 );
}

static qboolean R_PostInit( int w, int h )
{
	GLenum fmt, type;

	if( !post.tried )
	{
		post.tried = true;
		if( !GL_Support( GL_SHADER_GLSL100_EXT ) || !GL_Support( GL_SHADER_OBJECTS_EXT ) || gEngfuncs.Sys_CheckParm( "-gl2shim" ))
		{
			gEngfuncs.Con_Printf( "r_post: no GLSL shaders, post processing off\n" );
			return false;
		}
		pglGenFramebuffers = R_PostProc( "glGenFramebuffers" );
		pglBindFramebuffer = R_PostProc( "glBindFramebuffer" );
		pglFramebufferTexture2D = R_PostProc( "glFramebufferTexture2D" );
		pglCheckFramebufferStatus = R_PostProc( "glCheckFramebufferStatus" );
		pglDeleteFramebuffers = R_PostProc( "glDeleteFramebuffers" );
		if( !pglGenFramebuffers || !pglBindFramebuffer || !pglFramebufferTexture2D || !pglCheckFramebufferStatus || !pglDeleteFramebuffers )
		{
			gEngfuncs.Con_Printf( "r_post: no framebuffer objects, post processing off\n" );
			return false;
		}
		post.p_ssao = R_PostShader( "ssao", fs_ssao );
		post.p_aoblur = R_PostShader( "aoblur", fs_aoblur );
		post.p_ssr = R_PostShader( "ssr", fs_ssr );
		post.p_cube = R_PostShader( "cube", fs_cube );
		post.p_lum = R_PostShader( "lum", fs_lum );
		post.p_lumdown = R_PostShader( "lumdown", fs_lumdown );
		post.p_bright = R_PostShader( "bright", fs_bright );
		post.p_blur = R_PostShader( "blur", fs_blur );
		post.p_final = R_PostShader( "final", fs_final );
		if( !post.p_ssao || !post.p_aoblur || !post.p_ssr || !post.p_cube || !post.p_lum || !post.p_lumdown || !post.p_bright || !post.p_blur || !post.p_final )
			return false;
		post.exposure = 1.0f;
		post.ok = true;
	}
	if( !post.ok )
		return false;
	if( post.w == w && post.h == h )
		return true;

	R_PostFree();
	post.w = w; post.h = h;
	if( GL_Support( GL_ARB_TEXTURE_FLOAT_EXT ))
		fmt = GL_RGBA16F_ARB, type = GL_HALF_FLOAT_ARB;
	else fmt = GL_RGBA8, type = GL_UNSIGNED_BYTE;

	post.depth = R_PostTexture( w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST );
	if( !R_PostTarget( &post.rt[RT_SCENE], w, h, fmt, type, post.depth )
		|| !R_PostTarget( &post.rt[RT_AO], w / 2, h / 2, GL_RGBA8, GL_UNSIGNED_BYTE, 0 )
		|| !R_PostTarget( &post.rt[RT_AO2], w / 2, h / 2, GL_RGBA8, GL_UNSIGNED_BYTE, 0 )
		|| !R_PostTarget( &post.rt[RT_B1], w / 4, h / 4, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_B2], w / 4, h / 4, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_L64], 64, 64, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_L16], 16, 16, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_L4], 4, 4, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_L1], 1, 1, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_SSR], w / 2, h / 2, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_SSR2], w / 2, h / 2, fmt, type, 0 ))
	{
		gEngfuncs.Con_Printf( S_ERROR "r_post: framebuffer incomplete, post processing off\n" );
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
		R_PostFree();
		post.ok = false;
		R_PostRestoreState();
		return false;
	}
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_PostRestoreState();
	return true;
}

void R_InitPost( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_post );
	gEngfuncs.Cvar_RegisterVariable( &r_ssao );
	gEngfuncs.Cvar_RegisterVariable( &r_ssao_radius );
	gEngfuncs.Cvar_RegisterVariable( &r_ssao_strength );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_exposure );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_key );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_min );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_max );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_speed );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_bloom );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_bloom_threshold );
	gEngfuncs.Cvar_RegisterVariable( &r_post_saturation );
	gEngfuncs.Cvar_RegisterVariable( &r_post_sharpen );
	gEngfuncs.Cvar_RegisterVariable( &r_post_vignette );
	gEngfuncs.Cvar_RegisterVariable( &r_ssr );
	gEngfuncs.Cvar_RegisterVariable( &r_ssr_strength );
	gEngfuncs.Cvar_RegisterVariable( &r_post_debug );
	memset( &post, 0, sizeof( post ));
}

static qboolean R_PostWanted( void )
{
	int w = gpGlobals->width, h = gpGlobals->height;

	if( !r_post.value || tr.rotation != REF_ROTATE_NONE )
		return false;
	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return false;
	if( RI.rvp.viewport[0] != 0 || RI.rvp.viewport[1] != 0 || RI.rvp.viewport[2] != w || RI.rvp.viewport[3] != h )
		return false;
	return R_PostInit( w, h );
}

/*
==============
R_PostBegin

before the 3D view: draw it into the framebuffer
==============
*/
qboolean R_PostBegin( void )
{
	post.active = R_PostWanted();
	if( post.active )
	{
		pglBindFramebuffer( GL_FRAMEBUFFER, post.rt[RT_SCENE].fbo );
		post.tanx = tan( RI.rvp.fov_x * M_PI_F / 360.0f );
		post.tany = tan( RI.rvp.fov_y * M_PI_F / 360.0f );
		VectorCopy( RI.rvp.vieworigin, post.eye );
		// not RI.vforward: set later in R_RenderScene, it still holds the last side drawn around the player
		AngleVectors( RI.rvp.viewangles, post.vf, post.vr, post.vu );
	}
	return post.active;
}

static void R_PostBind( int unit, GLuint tex )
{
	pglActiveTextureARB( GL_TEXTURE0_ARB + unit );
	pglBindTexture( GL_TEXTURE_2D, tex );
}

static void R_PostPass( rtarget_t *rt, GLhandleARB prog )
{
	pglBindFramebuffer( GL_FRAMEBUFFER, rt ? rt->fbo : 0 );
	if( rt ) pglViewport( 0, 0, rt->w, rt->h );
	else pglViewport( 0, 0, post.w, post.h );
	pglUseProgramObjectARB( prog );
	pglBegin( GL_QUADS );
	pglTexCoord2f( 0, 0 ); pglVertex2f( -1, -1 );
	pglTexCoord2f( 1, 0 ); pglVertex2f( 1, -1 );
	pglTexCoord2f( 1, 1 ); pglVertex2f( 1, 1 );
	pglTexCoord2f( 0, 1 ); pglVertex2f( -1, 1 );
	pglEnd();
}

/*
==============
R_PostEnd

after the 3D view: the passes, and the result on the screen
==============
*/
void R_PostEnd( void )
{
	rtarget_t *rt = post.rt;
	float zfar = Q_max( 256.0f, RI.farClip );
	float tx = post.tanx, ty = post.tany;
	int w = post.w, h = post.h, i;
	float l1[4] = { 0 };
	GLuint cube_color, cube_depth;
	vec3_t cube_org;
	float cube_far[6];
	int cube_size;
	qboolean ssr, hecho = false;
	double dt;

	if( !post.active )
		return;
	ssr = R_PostScreenReflections( );
	post.active = false;

	// fullscreen quads with an identity projection
	pglMatrixMode( GL_PROJECTION ); pglPushMatrix(); pglLoadIdentity();
	pglMatrixMode( GL_MODELVIEW ); pglPushMatrix(); pglLoadIdentity();
	pglDisable( GL_DEPTH_TEST );
	pglDepthMask( GL_FALSE );
	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_CULL_FACE );
	pglColor4f( 1, 1, 1, 1 );

	// 1. ambient occlusion
	if( r_ssao.value && r_ssao_strength.value > 0.0f )
	{
		R_PostBind( 0, post.depth );
		pglUseProgramObjectARB( post.p_ssao );
		R_PostUniforms1i( post.p_ssao, "depthmap", 0 );
		R_PostUniform1f( post.p_ssao, "znear", 4.0f );
		R_PostUniform1f( post.p_ssao, "zfar", zfar );
		R_PostUniform2f( post.p_ssao, "tanfov", tx, ty );
		R_PostUniform1f( post.p_ssao, "radius", r_ssao_radius.value );
		R_PostUniform1f( post.p_ssao, "strength", r_ssao_strength.value );
		R_PostUniform2f( post.p_ssao, "texel", 1.0f / w, 1.0f / h );
		R_PostUniform2f( post.p_ssao, "dtexel", 1.0f / w, 1.0f / h );
		R_PostPass( &rt[RT_AO], post.p_ssao );

		R_PostBind( 1, rt[RT_AO].tex );
		pglUseProgramObjectARB( post.p_aoblur );
		R_PostUniforms1i( post.p_aoblur, "depthmap", 0 );
		R_PostUniforms1i( post.p_aoblur, "aomap", 1 );
		R_PostUniform1f( post.p_aoblur, "znear", 4.0f );
		R_PostUniform1f( post.p_aoblur, "zfar", zfar );
		R_PostUniform2f( post.p_aoblur, "tanfov", tx, ty );
		R_PostUniform2f( post.p_aoblur, "texel", 2.0f / w, 2.0f / h );
		R_PostUniform2f( post.p_aoblur, "dtexel", 1.0f / w, 1.0f / h );
		R_PostPass( &rt[RT_AO2], post.p_aoblur );
	}

	// 1b/1c. reflections around the player, or of the screen; blurred once
	if( ssr && R_BumpDynamicCube( &cube_color, &cube_depth, cube_org, cube_far, &cube_size ))
	{
		hecho = true;
		R_PostBind( 0, post.depth );
		R_PostBind( 1, rt[RT_SCENE].tex );
		R_PostBind( 2, cube_color );
		R_PostBind( 3, cube_depth );
		pglUseProgramObjectARB( post.p_cube );
		R_PostUniforms1i( post.p_cube, "depthmap", 0 );
		R_PostUniforms1i( post.p_cube, "scene", 1 );
		R_PostUniforms1i( post.p_cube, "cubecolor", 2 );
		R_PostUniforms1i( post.p_cube, "cubedepth", 3 );
		R_PostUniform1f( post.p_cube, "znear", 4.0f );
		R_PostUniform1f( post.p_cube, "zfar", zfar );
		R_PostUniform2f( post.p_cube, "tanfov", tx, ty );
		R_PostUniform2f( post.p_cube, "texel", 1.0f / w, 1.0f / h );
		R_PostUniform2f( post.p_cube, "dtexel", 1.0f / w, 1.0f / h );
		R_PostUniform1f( post.p_cube, "strength", r_ssr_strength.value );
		{
			GLint l = pglGetUniformLocationARB( post.p_cube, "cfar" );
			if( l >= 0 ) pglUniform1fvARB( l, 6, cube_far );
		}
		R_PostUniform1f( post.p_cube, "csize", cube_size );
		R_PostUniform3f( post.p_cube, "eye", post.eye );
		R_PostUniform3f( post.p_cube, "vf", post.vf );
		R_PostUniform3f( post.p_cube, "vr", post.vr );
		R_PostUniform3f( post.p_cube, "vu", post.vu );
		R_PostUniform3f( post.p_cube, "corg", cube_org );
		{
			GLuint ptex[2];
			vec4_t pn[2];
			vec3_t pe[2], pf[2], pr[2], pu[2];
			float ptx = 1.0f, pty = 1.0f;
			int np = R_BumpMirrors( ptex, pn, pe, pf, pr, pu, &ptx, &pty );
			GLint l;

			for( i = 0; i < np; i++ )
				R_PostBind( 4 + i, ptex[i] );
			R_PostUniforms1i( post.p_cube, "plano0", 4 );
			R_PostUniforms1i( post.p_cube, "plano1", 5 );
			R_PostUniform1f( post.p_cube, "nplanos", np );
			R_PostUniform2f( post.p_cube, "ptan", ptx, pty );
			if( np )
			{
				if(( l = pglGetUniformLocationARB( post.p_cube, "pn" )) >= 0 ) pglUniform4fvARB( l, np, (const float *)pn );
				if(( l = pglGetUniformLocationARB( post.p_cube, "pe" )) >= 0 ) pglUniform3fvARB( l, np, (const float *)pe );
				if(( l = pglGetUniformLocationARB( post.p_cube, "pf" )) >= 0 ) pglUniform3fvARB( l, np, (const float *)pf );
				if(( l = pglGetUniformLocationARB( post.p_cube, "pr" )) >= 0 ) pglUniform3fvARB( l, np, (const float *)pr );
				if(( l = pglGetUniformLocationARB( post.p_cube, "pu" )) >= 0 ) pglUniform3fvARB( l, np, (const float *)pu );
			}
			R_PostPass( &rt[RT_SSR], post.p_cube );
			for( i = 0; i < np; i++ )
			{
				pglActiveTextureARB( GL_TEXTURE0_ARB + 4 + i );
				pglBindTexture( GL_TEXTURE_2D, 0 );
			}
		}
		pglActiveTextureARB( GL_TEXTURE0_ARB + 3 ); pglBindTexture( GL_TEXTURE_2D, 0 );
		pglActiveTextureARB( GL_TEXTURE0_ARB + 2 ); pglBindTexture( GL_TEXTURE_2D, 0 );
	}
	else if( ssr && r_ssr.value )
	{
		hecho = true;
		R_PostBind( 0, post.depth );
		R_PostBind( 1, rt[RT_SCENE].tex );
		pglUseProgramObjectARB( post.p_ssr );
		R_PostUniforms1i( post.p_ssr, "depthmap", 0 );
		R_PostUniforms1i( post.p_ssr, "scene", 1 );
		R_PostUniform1f( post.p_ssr, "znear", 4.0f );
		R_PostUniform1f( post.p_ssr, "zfar", zfar );
		R_PostUniform2f( post.p_ssr, "tanfov", tx, ty );
		R_PostUniform1f( post.p_ssr, "strength", r_ssr_strength.value );
		R_PostUniform2f( post.p_ssr, "texel", 1.0f / w, 1.0f / h );
		R_PostUniform2f( post.p_ssr, "dtexel", 1.0f / w, 1.0f / h );
		R_PostPass( &rt[RT_SSR], post.p_ssr );
	}
	if( hecho )
	{

		R_PostBind( 0, rt[RT_SSR].tex );
		pglUseProgramObjectARB( post.p_blur );
		R_PostUniforms1i( post.p_blur, "src", 0 );
		R_PostUniform2f( post.p_blur, "dir", 1.0f / rt[RT_SSR].w, 0 );
		R_PostPass( &rt[RT_SSR2], post.p_blur );
		R_PostBind( 0, rt[RT_SSR2].tex );
		R_PostUniform2f( post.p_blur, "dir", 0, 1.0f / rt[RT_SSR2].h );
		R_PostPass( &rt[RT_SSR], post.p_blur );
	}

	// 2. eye adaptation: log of the brightness down to one pixel
	if( r_hdr.value )
	{
		R_PostBind( 0, rt[RT_SCENE].tex );
		pglUseProgramObjectARB( post.p_lum );
		R_PostUniforms1i( post.p_lum, "scene", 0 );
		R_PostUniform2f( post.p_lum, "texel", 1.0f / w * ( w / 256.0f ), 1.0f / h * ( h / 256.0f ));
		R_PostPass( &rt[RT_L64], post.p_lum );
		for( i = RT_L16; i <= RT_L1; i++ )
		{
			R_PostBind( 0, rt[i - 1].tex );
			pglUseProgramObjectARB( post.p_lumdown );
			R_PostUniforms1i( post.p_lumdown, "src", 0 );
			R_PostUniform2f( post.p_lumdown, "texel", 1.0f / rt[i - 1].w, 1.0f / rt[i - 1].h );
			R_PostPass( &rt[i], post.p_lumdown );
		}
		pglReadPixels( 0, 0, 1, 1, GL_RGBA, GL_FLOAT, l1 );
		{
			float media = exp( l1[0] );	// average linear brightness
			float objetivo = bound( r_hdr_min.value, r_hdr_key.value / Q_max( media, 0.001f ), r_hdr_max.value );

			dt = gp_host->realtime - post.lasttime;
			post.lasttime = gp_host->realtime;
			dt = bound( 0.0, dt, 0.25 );
			post.exposure += ( objetivo - post.exposure ) * ( 1.0f - exp( -dt * r_hdr_speed.value ));
			if( !isfinite( post.exposure )) post.exposure = 1.0f;
		}
	}
	else post.exposure = 1.0f;

	// 3. bloom
	R_PostBind( 0, rt[RT_SCENE].tex );
	pglUseProgramObjectARB( post.p_bright );
	R_PostUniforms1i( post.p_bright, "scene", 0 );
	R_PostUniform2f( post.p_bright, "texel", 1.0f / w, 1.0f / h );
	R_PostUniform1f( post.p_bright, "exposure", post.exposure * r_hdr_exposure.value );
	R_PostUniform1f( post.p_bright, "threshold", r_hdr_bloom_threshold.value );
	R_PostPass( &rt[RT_B1], post.p_bright );
	for( i = 0; i < 2; i++ )
	{
		R_PostBind( 0, rt[RT_B1].tex );
		pglUseProgramObjectARB( post.p_blur );
		R_PostUniforms1i( post.p_blur, "src", 0 );
		R_PostUniform2f( post.p_blur, "dir", 1.0f / rt[RT_B1].w, 0 );
		R_PostPass( &rt[RT_B2], post.p_blur );
		R_PostBind( 0, rt[RT_B2].tex );
		R_PostUniform2f( post.p_blur, "dir", 0, 1.0f / rt[RT_B2].h );
		R_PostPass( &rt[RT_B1], post.p_blur );
	}

	// 4. on the screen
	R_PostBind( 0, rt[RT_SCENE].tex );
	R_PostBind( 1, rt[RT_AO2].tex );
	R_PostBind( 2, rt[RT_B1].tex );
	R_PostBind( 3, rt[RT_SSR].tex );
	pglUseProgramObjectARB( post.p_final );
	R_PostUniforms1i( post.p_final, "scene", 0 );
	R_PostUniforms1i( post.p_final, "aomap", 1 );
	R_PostUniforms1i( post.p_final, "bloom", 2 );
	R_PostUniforms1i( post.p_final, "ssrmap", 3 );
	R_PostUniform1f( post.p_final, "ssrk", hecho ? 1.0f : 0.0f );
	R_PostUniform2f( post.p_final, "texel", 1.0f / w, 1.0f / h );
	R_PostUniform1f( post.p_final, "exposure", post.exposure * r_hdr_exposure.value );
	R_PostUniform1f( post.p_final, "bloomk", r_hdr_bloom.value );
	R_PostUniform1f( post.p_final, "aok", r_ssao.value ? 1.0f : 0.0f );
	R_PostUniform1f( post.p_final, "sat", r_post_saturation.value );
	R_PostUniform1f( post.p_final, "sharpen", r_post_sharpen.value );
	R_PostUniform1f( post.p_final, "vignette", r_post_vignette.value );
	R_PostUniform1f( post.p_final, "hdr", r_hdr.value ? 1.0f : 0.0f );
	R_PostUniforms1i( post.p_final, "debug", (int)r_post_debug.value );
	R_PostPass( NULL, post.p_final );

	pglActiveTextureARB( GL_TEXTURE0_ARB + 3 ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE2_ARB ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE1_ARB ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglMatrixMode( GL_PROJECTION ); pglPopMatrix();
	pglMatrixMode( GL_MODELVIEW ); pglPopMatrix();
	pglViewport( 0, 0, w, h );
	pglEnable( GL_DEPTH_TEST );
	pglDepthMask( GL_TRUE );
	R_PostRestoreState();
}

// the glossy surfaces leave the mask for 1b or 1c (gl_bump.c asks it while drawing)
qboolean R_PostScreenReflections( void )
{
	return post.active && r_ssr_strength.value > 0.0f && ( r_ssr.value || R_BumpDynamicOn( ));
}

qboolean R_PostRunning( void )
{
	return r_post.value && post.ok;
}
