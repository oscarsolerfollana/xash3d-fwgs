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
CVAR_DEFINE_AUTO( r_hdr_key, "0.02", FCVAR_ARCHIVE, "custom: average brightness the eye adapts to (log average of the linear view)" );
CVAR_DEFINE_AUTO( r_hdr_min, "0.35", FCVAR_ARCHIVE, "custom: lowest automatic exposure (bright places)" );
CVAR_DEFINE_AUTO( r_hdr_max, "3", FCVAR_ARCHIVE, "custom: highest automatic exposure (dark places)" );
CVAR_DEFINE_AUTO( r_hdr_fuerza, "0.75", FCVAR_ARCHIVE, "custom: how much the eye adapts (0 nothing, 1 all the way to r_hdr_key)" );
CVAR_DEFINE_AUTO( r_hdr_speed, "0.5", FCVAR_ARCHIVE, "custom: eye adaptation speed" );
CVAR_DEFINE_AUTO( r_hdr_bloom, "0.6", FCVAR_ARCHIVE, "custom: bloom strength" );
CVAR_DEFINE_AUTO( r_hdr_bloom_threshold, "0.75", FCVAR_ARCHIVE, "custom: brightness (after exposure) above which things glow" );
CVAR_DEFINE_AUTO( r_post_saturation, "1.05", FCVAR_ARCHIVE, "custom: colour saturation" );
CVAR_DEFINE_AUTO( r_post_sharpen, "0.25", FCVAR_ARCHIVE, "custom: sharpening" );
CVAR_DEFINE_AUTO( r_post_lente, "1", FCVAR_ARCHIVE, "custom: lens flare (ghosts and halo of the bright lights) looking through a zoom, as strong as the zoom (0 off)" );
CVAR_DEFINE_AUTO( r_post_rayos, "0", FCVAR_ARCHIVE, "custom: shafts of light from the sun through the gaps (0 off, also with F8: Oscar does not want them)" );
CVAR_DEFINE_AUTO( r_post_fxaa, "1", FCVAR_ARCHIVE, "custom: smooths the edges (FXAA): the multisampling of gl_msaa does not reach the image of the post processing" );
CVAR_DEFINE_AUTO( r_post_movimiento, "0.5", FCVAR_ARCHIVE, "custom: motion blur of the camera, as the shutter open that part of a frame at 60 fps (0 off, 1 the whole frame)" );
CVAR_DEFINE_AUTO( r_post_sombras, "0.5", FCVAR_ARCHIVE, "custom: lifts the dark and half lit parts, black and white stay (0 off, 1 a lot)" );
CVAR_DEFINE_AUTO( r_post_vignette, "0.25", FCVAR_ARCHIVE, "custom: darker corners" );
CVAR_DEFINE_AUTO( r_ssr, "0", FCVAR_ARCHIVE, "custom: reflections of what is on the screen on glossy surfaces" );
CVAR_DEFINE_AUTO( r_ssr_strength, "1", FCVAR_ARCHIVE, "custom: strength of the screen reflections" );
CVAR_DEFINE_AUTO( r_reflect_luz, "1", FCVAR_ARCHIVE, "custom: the mirrors as bright as the light on the surface (0 the same everywhere, 1 dark in the shadows)" );
CVAR_DEFINE_AUTO( r_reflect_luz_ref, "0.45", FCVAR_ARCHIVE, "custom: brightness of a surface in full light, for r_reflect_luz" );
CVAR_DEFINE_AUTO( r_reflect_halo, "1", FCVAR_ARCHIVE, "custom: size of the circle the lights in the mirrors open in (0 none, 2 twice as wide)" );
CVAR_DEFINE_AUTO( r_reflect_halo_fuerza, "1", FCVAR_ARCHIVE, "custom: how bright that circle is (0.5 half)" );
CVAR_DEFINE_AUTO( r_reflect_bloom, "2.5", FCVAR_ARCHIVE, "custom: bloom of the lights in the mirrors, times the bloom of the rest" );
CVAR_DEFINE_AUTO( r_reflect_luces, "1.5", FCVAR_ARCHIVE, "custom: how much more the lights shine in the mirrors than what is only lit (0 the same)" );
CVAR_DEFINE_AUTO( r_reflect_luz_curva, "2", FCVAR_ARCHIVE, "custom: how fast the mirrors fade in the dark (1 in proportion to the light, 2 its square)" );
CVAR_DEFINE_AUTO( r_lupa, "1", 0, "custom: the 3D view made bigger about its middle, as a magnifying glass, the fov as it is (1 off; ads_editar uses it)" );
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

enum { RT_SCENE, RT_AO, RT_AO2, RT_B1, RT_B2, RT_L64, RT_L16, RT_L4, RT_L1, RT_SSR, RT_SSR2, RT_FINAL, RT_RAY1, RT_RAY2, RT_COUNT };

static struct
{
	qboolean	tried, ok;
	int		w, h;
	GLuint		depth;
	rtarget_t	rt[RT_COUNT];
	GLhandleARB	p_ssao, p_aoblur, p_ssr, p_cube, p_bright, p_blur, p_lum, p_lumdown, p_final, p_mprof, p_fxaa, p_rayfuente, p_rayos;
	vec3_t		eye, vf, vr, vu;	// of the scene, for the reflections around the player
	float		exposure;
	double		lasttime;
	float		tanx, tany;	// of the scene: the view model changes the fov before R_PostEnd
	qboolean	active;		// this frame goes through the framebuffer
	vec3_t		peye, pvf, pvr, pvu;	// the camera of the frame before, for the motion blur
	double		ptime;
	qboolean	pvalid;
	float		rayosk;
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
"uniform float strength, luz, luzref, luzcurva, luces;\n"
"vec3 lado( vec3 p, vec3 a, vec3 b ) { return abs( a.z - p.z ) < abs( b.z - p.z ) ? a - p : p - b; }\n"
"vec2 pantalla( vec3 q ) { return q.xy / ( -q.z * tanfov ) * 0.5 + 0.5; }\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	float al = texture2D( scene, uv ).a;\n"
"	al -= 32.0 * floor( al / 32.0 );\n"	// gl_bump.c: 32 more if it may be a mirror, then the roughness as twice the whole part
"	float rq = floor( al * 0.5 );\n"
"	float m = clamp( 1.0 - ( al - 2.0 * rq ), 0.0, 1.0 ), rug = rq / 15.0;\n"
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
	// rough: a cone as wide as the reflection is long (the mirrors: 0.3 of roughness, a lamp 40 pixels)
"	vec3 c = texture2D( scene, hit ).rgb;\n"
"	float rad = min( rug * 0.3 * length( q - p ) / ( -p.z * tanfov.y ) / texel.y * 0.5, 40.0 );\n"
"	if( rad > 1.0 )\n"
"	{\n"
"		float giro = fract( sin( dot( floor( gl_FragCoord.xy * 0.5 ), vec2( 12.9898, 78.233 ))) * 43758.5453 ) * 6.2832;\n"
"		vec3 sc = vec3( 0.0 ); float ws = 0.0;\n"
"		for( int i = 0; i < 10; i++ )\n"
"		{\n"
"			float a = float( i ) * 2.39996 + giro, dd = sqrt(( float( i ) + 0.5 ) / 10.0 );\n"
"			float w = exp( -2.5 * dd * dd );\n"
"			sc += texture2D( scene, hit + vec2( cos( a ), sin( a )) * dd * rad * texel ).rgb * w;\n"
"			ws += w;\n"
"		}\n"
"		c = sc / ws;\n"
"	}\n"
	// as the mirrors: dark in the shadows (r_reflect_luz), the lights brighter than what is only lit
"	float sombra = 1.0;\n"
"	if( luz > 0.0 )\n"
"	{\n"
"		float lz = 0.0;\n"
"		for( int i = 0; i < 4; i++ )\n"
"		{\n"
"			float a = float( i ) * 1.570796 + 0.39;\n"
"			vec3 sl = texture2D( scene, uv + vec2( cos( a ), sin( a )) * texel * 6.0 ).rgb;\n"
"			lz += max( sl.r, max( sl.g, sl.b ));\n"
"		}\n"
"		sombra = mix( 1.0, pow( clamp( lz / 4.0 / luzref, 0.0, 1.0 ), luzcurva ), luz );\n"
"	}\n"
"	float b = max( c.r, max( c.g, c.b ));\n"
"	f *= sombra;\n"
"	gl_FragColor = vec4( c * f * ( 1.0 + luces * smoothstep( 0.6, 0.95, b )), f );\n"
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
"uniform sampler2D plano0, plano1, plano2, plano3, plano4, plano5, plano6, plano7, plano8, plano9;\n"	// the mirrors (gl_bump.c R_BumpPlanes)
"uniform vec4 pn[10];\n"
"uniform vec3 pe[10], pf[10], pr[10], pu[10];\n"
"uniform vec2 ptan;\n"
"uniform float nplanos, cubo, luz, luzref, luzcurva, luces, halo, halok, dbg;\n"
"uniform float pblur[10];\n"
"uniform float pfade[10];\n"	// a new mirror comes in softly
// where the mirror view saw this point of its plane
"vec3 espejoPunto( vec2 c, int k ) { vec2 q = ( c * 2.0 - 1.0 ) * ptan; return pe[k] + pf[k] + pr[k] * q.x + pu[k] * q.y; }\n"
"vec2 espejo( vec3 P, int k ) { vec3 v = P - pe[k]; return vec2( dot( v, pr[k] ), dot( v, pu[k] )) / ( dot( v, pf[k] ) * ptan ) * 0.5 + 0.5; }\n"
// a rough surface reflects a cone of directions: what is reflected blurs as much as it is far from the
// surface (sharp where it touches it, lamps on the ceiling a wide spot), the same seen near or far,
// and longer towards the eye the more grazing the look
"float zmirror( sampler2D t, vec2 c ) { return texture2D( t, c, -16.0 ).a; }\n"
// where the image of mirror k shows what the reflected eye of now sees through P: the image can be of a
// frame before (r_reflect_cada), drawn from where the eye was; with its depth, walked along the ray of now
// (parallax), so it does not jump back and forth while moving
"vec2 reproyecta( sampler2D t, vec3 P, int k )\n"
"{\n"
"	vec2 c = espejo( P, k );\n"
"	vec3 en = eye - 2.0 * ( dot( eye, pn[k].xyz ) - pn[k].w ) * pn[k].xyz;\n"
"	if( dot( en - pe[k], en - pe[k] ) < 0.01 ) return c;\n"
"	vec3 dn = normalize( P - en );\n"
"	for( int i = 0; i < 3; i++ )\n"
"	{\n"
"		vec3 dir = normalize( espejoPunto( c, k ) - pe[k] );\n"
"		float z = zmirror( t, c );\n"
"		vec3 Q = pe[k] + dir * ( z / max( dot( dir, pf[k] ), 0.05 ));\n"
"		float tq = max( dot( Q - en, dn ), 1.0 );\n"
"		c = espejo( en + dn * tq, k );\n"
"	}\n"
"	return c;\n"
"}\n"
"vec3 rugoso( sampler2D t, vec3 P, vec3 N, int k, float rough, out vec2 c, out vec2 eje, out float largo )\n"
"{\n"
"	c = reproyecta( t, P, k );\n"
"	eje = vec2( 0.0, 1.0 ); largo = 1.0;\n"
"	if( rough <= 0.0 ) return vec3( 0.0 );\n"
"	vec3 v = P - pe[k];\n"
"	vec3 dir = normalize( v );\n"
"	float zq = zmirror( t, c );\n"
"	vec3 Q = pe[k] + dir * ( zq / max( dot( dir, pf[k] ), 0.05 ));\n"	// what the mirror sees there
"	float altura = length( Q - P );\n"
"	float lejos = max( length( Q - pe[k] ), 1.0 );\n"
"	float r = rough * altura / lejos / ( 2.0 * ptan.y ) / texel.y;\n"	// the cone, in pixels of the mirror image
	// towards the eye: the plane normal as the mirror image sees it, stretched as the look grazes
"	vec2 cn = espejo( P + N * 16.0, k ) - c;\n"
"	if( dot( cn, cn ) > 1e-10 ) eje = normalize( cn / texel );\n"
"	float rasante = abs( dot( N, normalize( eye - P )));\n"
"	largo = clamp( 1.0 / max( rasante, 0.5 ), 1.0, 2.0 );\n"
"	return vec3( min( r, 40.0 ), 0.0, 0.0 );\n"
"}\n"
"vec3 difuso( sampler2D t, vec2 c, float r, vec2 eje, float largo )\n"	// a bell of taps; the bright ones weigh more, so lights open in circles
"{\n"
"	vec3 s = vec3( 0.0 );\n"
"	float ws = 0.0;\n"
"	vec2 per = vec2( -eje.y, eje.x );\n"
"	float giro = fract( sin( dot( floor( gl_FragCoord.xy * 0.5 ), vec2( 12.9898, 78.233 ))) * 43758.5453 ) * 6.2832;\n"	// the same in each 2x2 block: the card picks the mip level from the change inside the block, and a turn of its own in each pixel made it read the smallest one (a dark smear, with sharp spots where it did not: stars fixed on the screen)
"	float lod = max( log2( r * 0.35 ), 0.0 );\n"	// taps this far apart: each one the average of its share
"	for( int i = 0; i < 10; i++ )\n"
"	{\n"
"		float a = float( i ) * 2.39996 + giro, d = sqrt(( float( i ) + 0.5 ) / 10.0 );\n"
"		vec2 o = eje * cos( a ) * largo + per * sin( a );\n"
"		vec3 m = texture2D( t, c + o * d * r * texel, lod ).rgb;\n"
"		float w = r < 0.5 ? ( i == 0 ? 1.0 : 0.0 ) : exp( -2.5 * d * d );\n"
"		s += m * w;\n"
"		ws += w;\n"
"	}\n"
"	s /= ws;\n"
	// over white is kept: cut after the blur of the reflections, in the final pass
"	if( halo > 0.0 && r > 1.0 )\n"	// as wide as the spot: with a floor of 6 pixels the small far ones got a halo as big as the near ones
"	{\n"
"		vec3 hl = vec3( 0.0 );\n"
"		float hr = r * 3.0 * halo;\n"
"		float hlod = max( log2( hr * 0.3 ), 0.0 );\n"
"		for( int i = 0; i < 10; i++ )\n"
"		{\n"
"			float a = float( i ) * 2.39996 + 1.0 + giro, d = sqrt(( float( i ) + 0.5 ) / 10.0 );\n"
"			vec3 m = texture2D( t, c + ( eje * cos( a ) * largo + per * sin( a )) * d * hr * texel, hlod ).rgb;\n"
"			hl += max( m - 0.6, 0.0 ) * exp( -1.2 * d * d );\n"
"		}\n"
"		s += hl / 10.0 * 2.5 * halok;\n"
"	}\n"
"	return s;\n"
"}\n"

// out of what the mirror view saw (looking down, the floor near the feet): the edge pixel stretched
// came out as long bright bands moving with the player; there the reflection fades out
"float borde = 1.0;\n"
"vec3 mira( sampler2D t, vec3 P, vec3 N, int k, float rough )\n"
"{\n"

"	vec2 c, eje; float largo;\n"
"	float r = rugoso( t, P, N, k, rough, c, eje, largo ).x;\n"
"	if( dbg > 6.5 ) return vec3( r / 48.0, zmirror( t, c ) / 2000.0, 0.0 );\n"	// 7: the blur radius (red), the mirror depth (green)
"	vec2 e = min( c, 1.0 - c );\n"
"	borde = clamp( min( e.x, e.y ) / 0.04, 0.0, 1.0 );\n"
"	return difuso( t, c, r, eje, largo );\n"
"}\n"
"vec4 refleja( vec3 r, float f, float sombra )\n"
"{\n"
"	float b = max( r.r, max( r.g, r.b ));\n"
"	float k = smoothstep( 0.6, 0.95, b );\n"	// how much of a light it is
"	float g = f * sombra;\n"
"	if( dbg > 8.5 ) return vec4( f, sombra, borde, 1.0 );\n"	// 9: strength, shadow, edge
"	if( dbg > 6.5 ) return vec4( r, 1.0 );\n"
"	if( dbg > 5.5 ) return vec4( 1.0, 0.0, 1.0, 1.0 );\n"
"	return vec4( r * g * ( 1.0 + luces * k ), g );\n"
"}\n"
"bool enplano( vec3 P, vec3 N, int k ) { float h = abs( dot( P, pn[k].xyz ) - pn[k].w ), e = length( P - eye ); return h < 2.0 + 0.004 * e && ( dot( N, pn[k].xyz ) > 0.8 || h < 1.0 + 0.002 * e ); }\n"
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
"	float al = texture2D( scene, uv ).a, esplano = floor( al / 32.0 );\n"	// gl_bump.c: 32 more if its texture may be a mirror
"	al -= 32.0 * esplano;\n"
"	float rq = floor( al * 0.5 );\n"
"	float m = clamp( 1.0 - ( al - 2.0 * rq ), 0.0, 1.0 ), rug = rq / 15.0;\n"	// the roughness over it
"	float d = texture2D( depthmap, centro( uv )).r;\n"
"	gl_FragColor = vec4( 0.0 );\n"
"	if( dbg > 4.5 && dbg < 5.5 ) { gl_FragColor = vec4( vec3( m ), 0.0 ); return; }\n"
"	if( m < 0.01 || d < 0.3 || d >= 0.9999 ) return;\n"
"	vec3 p = posicion( uv );\n"
"	vec3 dx = lado( p, posicion( uv + vec2( texel.x, 0.0 )), posicion( uv - vec2( texel.x, 0.0 )));\n"
"	vec3 dy = lado( p, posicion( uv + vec2( 0.0, texel.y )), posicion( uv - vec2( 0.0, texel.y )));\n"
"	vec3 n = normalize( cross( dx, dy ));\n"
"	if( dot( n, p ) > 0.0 ) n = -n;\n"
"	vec3 P = eye + vr * p.x + vu * p.y - vf * p.z;\n"
"	vec3 N = normalize( vr * n.x + vu * n.y - vf * n.z );\n"
"	float f = m * strength, sombra = 1.0;\n"
"	if( luz > 0.0 )\n"
"	{\n"
"		float lz = 0.0;\n"
"		for( int i = 0; i < 4; i++ )\n"
"		{\n"
"			float a = float( i ) * 1.570796 + 0.39;\n"
"			vec3 sc = texture2D( scene, uv + vec2( cos( a ), sin( a )) * texel * 6.0 ).rgb;\n"
"			lz += max( sc.r, max( sc.g, sc.b ));\n"
"		}\n"
"		sombra = mix( 1.0, pow( clamp( lz / 4.0 / luzref, 0.0, 1.0 ), luzcurva ), luz );\n"
"	}\n"
"	float np = esplano < 0.5 ? 0.0 : nplanos;\n"	// a texture that is always the cube, even on the plane of a mirror
"	if( np > 0.5 && enplano( P, N, 0 )) { gl_FragColor = refleja( mira( plano0, P, N, 0, pblur[0] ), f * borde * pfade[0], sombra ); return; }\n"
"	if( np > 1.5 && enplano( P, N, 1 )) { gl_FragColor = refleja( mira( plano1, P, N, 1, pblur[1] ), f * borde * pfade[1], sombra ); return; }\n"
"	if( np > 2.5 && enplano( P, N, 2 )) { gl_FragColor = refleja( mira( plano2, P, N, 2, pblur[2] ), f * borde * pfade[2], sombra ); return; }\n"
"	if( np > 3.5 && enplano( P, N, 3 )) { gl_FragColor = refleja( mira( plano3, P, N, 3, pblur[3] ), f * borde * pfade[3], sombra ); return; }\n"
"	if( np > 4.5 && enplano( P, N, 4 )) { gl_FragColor = refleja( mira( plano4, P, N, 4, pblur[4] ), f * borde * pfade[4], sombra ); return; }\n"
"	if( np > 5.5 && enplano( P, N, 5 )) { gl_FragColor = refleja( mira( plano5, P, N, 5, pblur[5] ), f * borde * pfade[5], sombra ); return; }\n"
"	if( np > 6.5 && enplano( P, N, 6 )) { gl_FragColor = refleja( mira( plano6, P, N, 6, pblur[6] ), f * borde * pfade[6], sombra ); return; }\n"
"	if( np > 7.5 && enplano( P, N, 7 )) { gl_FragColor = refleja( mira( plano7, P, N, 7, pblur[7] ), f * borde * pfade[7], sombra ); return; }\n"
"	if( np > 8.5 && enplano( P, N, 8 )) { gl_FragColor = refleja( mira( plano8, P, N, 8, pblur[8] ), f * borde * pfade[8], sombra ); return; }\n"
"	if( np > 9.5 && enplano( P, N, 9 )) { gl_FragColor = refleja( mira( plano9, P, N, 9, pblur[9] ), f * borde * pfade[9], sombra ); return; }\n"
"	if( cubo < 0.5 ) return;\n"	// only the mirrors: the rest does not reflect
"	vec3 R = reflect( normalize( P - eye ), N );\n"
	// in steps from the surface outwards, until the ray is behind what the eye sees in that direction
	// (not far behind: that is passing behind something), then halved down; a guess that jumped to
	// the answer reflected the door on itself, and from one frame to the next took one answer or another
"	vec3 o = P - corg + N * 1.0;\n"
	// the steps grow by 1.15 (4 the first); each pixel takes them a part of a step further along that growth, at
	// every distance: with all at the same places, neighbours found a thin thing (the ceiling of a corridor) at
	// the same steps and it came out as a ladder of copies on the floor
"	float t = fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ))));\n"
"	float paso = 4.0, antes = 0.0, s = 0.0;\n"
"	vec3 X = o + R * 8192.0;\n"
"	for( int i = 0; i < 40; i++ )\n"
"	{\n"
"		antes = s;\n"
"		s = 4.0 * ( pow( 1.15, float( i ) + t ) - 1.0 ) / 0.15 + 4.0 * t;\n"
"		paso = max( s - antes, 0.5 ) * 1.15;\n"
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
"	vec3 D = normalize( X );\n"
"	vec3 c = texture2D( cubecolor, cara( D, mm, cl )).rgb;\n"
	// rough: a cone of directions around it (the sides are small: a lamp was a sharp square of a few pixels)
"	float ang = rug * 0.15 + 1.5 / csize;\n"	// as wide as the mirrors open a lamp over a floor (about 0.13 at its roughness); at least a pixel and a half of a side, so the squares of the small sides blur
"	{\n"
"		vec3 t1 = normalize( cross( D, abs( D.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 )));\n"
"		vec3 t2 = cross( D, t1 );\n"
"		float giro = fract( sin( dot( floor( gl_FragCoord.xy * 0.5 ), vec2( 12.9898, 78.233 ))) * 43758.5453 ) * 6.2832;\n"
"		vec3 sc = c; float ws = 1.0;\n"
"		for( int i = 0; i < 10; i++ )\n"
"		{\n"
"			float a = float( i ) * 2.39996 + giro, dd = sqrt(( float( i ) + 0.5 ) / 10.0 );\n"
"			float w = exp( -2.5 * dd * dd );\n"
"			vec3 Di = normalize( D + ( t1 * cos( a ) + t2 * sin( a )) * dd * ang );\n"
"			sc += texture2D( cubecolor, cara( Di, mm, cl )).rgb * w;\n"
"			ws += w;\n"
"		}\n"
"		c = sc / ws;\n"
"	}\n"
	// as the mirrors: dark in the shadows, the lights brighter than what is only lit
"	gl_FragColor = refleja( c, f, sombra );\n"
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
"uniform sampler2D scene, ssrmap;\n"
"uniform vec2 texel;\n"
"uniform float exposure, threshold, ssrk, rbloom;\n"
"void main( void )\n"
"{\n"
"	vec3 o = vec3( 0.0 );\n"
	// the 16 pixels of the block, each over the threshold on its own: averaged first, a lamp far away
	// (a few pixels) fell under it and lost its glow, or not, depending on where it fell
"	for( int x = 0; x < 4; x++ )\n"
"	for( int y = 0; y < 4; y++ )\n"
"	{\n"
"		vec2 q = gl_TexCoord[0].xy + ( vec2( float( x ), float( y )) - 1.5 ) * texel;\n"
"		vec4 rf = texture2D( ssrmap, q ) * ssrk;\n"
"		rf.rgb /= 1.0 + max( max( rf.r, max( rf.g, rf.b )) - 1.0, 0.0 );\n"
"		float lk = smoothstep( 0.6, 0.95, max( rf.r, max( rf.g, rf.b )) / max( rf.a, 0.05 ));\n"	// only the lights in it
"		vec3 c = pow( texture2D( scene, q ).rgb * ( 1.0 - rf.a ) + rf.rgb * mix( 1.0, rbloom, lk ), vec3( 2.2 )) * exposure;\n"
"		float l = dot( c, vec3( 0.2126, 0.7152, 0.0722 ));\n"
"		o += c * ( max( l - threshold, 0.0 ) / max( l, 0.0001 ));\n"
"	}\n"
"	gl_FragColor = vec4( o / 16.0, 1.0 );\n"
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

// FXAA (Lottes): where the brightness changes sharply, a few taps along the edge it runs on
static const char *fs_fxaa =
"uniform sampler2D src;\n"
"uniform vec2 texel;\n"
"float lu( vec3 c ) { return dot( c, vec3( 0.299, 0.587, 0.114 )); }\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	vec3 m = texture2D( src, uv ).rgb;\n"
"	float lm = lu( m );\n"
"	float nw = lu( texture2D( src, uv + vec2( -1.0, -1.0 ) * texel ).rgb );\n"
"	float ne = lu( texture2D( src, uv + vec2( 1.0, -1.0 ) * texel ).rgb );\n"
"	float sw = lu( texture2D( src, uv + vec2( -1.0, 1.0 ) * texel ).rgb );\n"
"	float se = lu( texture2D( src, uv + vec2( 1.0, 1.0 ) * texel ).rgb );\n"
"	float lmin = min( lm, min( min( nw, ne ), min( sw, se )));\n"
"	float lmax = max( lm, max( max( nw, ne ), max( sw, se )));\n"
"	if( lmax - lmin < max( 0.0312, lmax * 0.125 )) { gl_FragColor = vec4( m, 1.0 ); return; }\n"
"	vec2 dir = vec2( -(( nw + ne ) - ( sw + se )), ( nw + sw ) - ( ne + se ));\n"
"	float red = max(( nw + ne + sw + se ) * 0.25 * 0.125, 1.0 / 128.0 );\n"
"	float rcp = 1.0 / ( min( abs( dir.x ), abs( dir.y )) + red );\n"
"	dir = clamp( dir * rcp, vec2( -8.0 ), vec2( 8.0 )) * texel;\n"
"	vec3 a = 0.5 * ( texture2D( src, uv + dir * ( 1.0 / 3.0 - 0.5 )).rgb + texture2D( src, uv + dir * ( 2.0 / 3.0 - 0.5 )).rgb );\n"
"	vec3 b = a * 0.5 + 0.25 * ( texture2D( src, uv - dir * 0.5 ).rgb + texture2D( src, uv + dir * 0.5 ).rgb );\n"
"	float lb = lu( b );\n"
"	gl_FragColor = vec4(( lb < lmin || lb > lmax ) ? a : b, 1.0 );\n"
"}\n";

// light shafts: the sky where it is seen, dragged towards the sun (the lamps have the bloom; dragged
// like the sky they left a string of copies). 1: the sky, at a quarter of the size
static const char *fs_rayfuente =
FS_COMMON
"uniform sampler2D scene;\n"
"uniform vec2 texel;\n"
"uniform float exposure;\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	vec3 s = vec3( 0.0 );\n"
"	float cielo = 0.0;\n"
"	for( int x = 0; x < 2; x++ )\n"
"	for( int y = 0; y < 2; y++ )\n"
"	{\n"
"		vec2 q = uv + ( vec2( float( x ), float( y )) - 0.5 ) * texel * 2.0;\n"
"		vec3 c = pow( texture2D( scene, q ).rgb, vec3( 2.2 )) * exposure;\n"
"		float d = texture2D( depthmap, q ).r;\n"
"		if( d >= 0.9999 ) { s += c; cielo += 1.0; }\n"
"	}\n"
"	gl_FragColor = vec4( s * 0.25, cielo * 0.25 );\n"
"}\n";

// 2. dragged towards the sun on the screen, fading on the way; each pixel starts the steps at its own
// offset so they blend into a beam instead of copies
static const char *fs_rayos =
"uniform sampler2D src;\n"
"uniform vec2 sol;\n"	// the sun on the screen
"uniform float solk;\n"	// how much the sun is in front (0 behind)
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	vec3 s = vec3( 0.0 );\n"
"	vec2 d = ( uv - sol ) * ( 0.9 / 24.0 );\n"
"	vec2 q = uv - d * fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ))));\n"
"	float w = 1.0, ws = 0.0;\n"
"	for( int i = 0; i < 24; i++ )\n"
"	{\n"
"		vec4 c = texture2D( src, q );\n"
"		s += c.rgb * w * solk;\n"
"		ws += w;\n"
"		w *= 0.94;\n"
"		q -= d;\n"
"	}\n"
"	gl_FragColor = vec4( s / ws * 2.0, 1.0 );\n"
"}\n";

// the depth of a mirror view, along it, into the alpha of its image: the roughness reads it there
// (with a depth texture per mirror, 10 of them went over what the card samples in one pass)
static const char *fs_mprof =
"uniform sampler2D dt;\n"
"uniform float zf;\n"
"void main( void )\n"
"{\n"
"	float z = texture2D( dt, gl_TexCoord[0].xy ).r * 2.0 - 1.0;\n"
"	gl_FragColor = vec4( 0.0, 0.0, 0.0, 2.0 * 4.0 * zf / ( zf + 4.0 - z * ( zf - 4.0 )));\n"
"}\n";

static const char *fs_final =
FS_COMMON
"uniform sampler2D scene, aomap, bloom, ssrmap;\n"
"uniform vec3 eye, vf, vr, vu, peye, pvf, pvr, pvu;\n"
"uniform float movimiento;\n"
"uniform vec2 texel;\n"
"uniform float exposure, bloomk, aok, ssrk, sat, sharpen, vignette, hdr, sombras, rayosk;\n"
"uniform sampler2D rayos;\n"
"uniform float lentek;\n"
"uniform int debug;\n"
// lens flare: only what is very bright, seen again as small copies of it through the centre of the lens
// (each ghost the light scaled down about the centre, some mirrored), a thin ring round in the screen
// (not stretched to 16:9) and a little colour apart; only through a zoom (R_PostLente): the eye has no lens
"vec3 brilla( vec2 p ) { vec3 c = texture2D( bloom, p ).rgb; return max( c - 0.35, 0.0 ); }\n"
"vec3 lente( vec2 uv )\n"
"{\n"
"	float asp = texel.y / texel.x;\n"
"	vec2 d = uv - 0.5;\n"
"	vec3 s = vec3( 0.0 );\n"
"	for( int i = 0; i < 4; i++ )\n"
"	{\n"
"		float e = i == 0 ? -1.6 : ( i == 1 ? -3.0 : ( i == 2 ? 2.2 : -5.0 ));\n"	// how many times smaller, and mirrored when negative
"		vec2 p = 0.5 + d * e;\n"
"		if( p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 ) continue;\n"
"		float w = 1.0 - smoothstep( 0.25, 0.5, length(( p - 0.5 ) * vec2( asp, 1.0 )) / asp );\n"
"		vec2 ab = d * 0.004 * abs( e );\n"
"		vec3 c = vec3( brilla( p + ab ).r, brilla( p ).g, brilla( p - ab ).b );\n"
"		vec3 tinte = i == 0 ? vec3( 1.0, 0.7, 0.4 ) : ( i == 1 ? vec3( 0.5, 0.8, 1.0 ) : ( i == 2 ? vec3( 0.7, 1.0, 0.6 ) : vec3( 1.0, 0.6, 0.9 )));\n"
"		s += c * w * tinte * 0.6;\n"
"	}\n"
"	vec2 da = d * vec2( asp, 1.0 );\n"
"	float rd = length( da );\n"
"	vec2 hp = 0.5 - normalize( da + 1e-5 ) * 0.42 / vec2( asp, 1.0 );\n"	// the ring: where the light would be on a circle round the centre
"	float anillo = exp( -pow(( rd - 0.42 ) / 0.02, 2.0 ));\n"
"	s += brilla( hp ) * anillo * vec3( 0.7, 0.85, 1.0 ) * 0.5;\n"
"	return s;\n"
"}\n"
"vec3 aces( vec3 x ) { return clamp(( x * ( 2.51 * x + 0.03 )) / ( x * ( 2.43 * x + 0.59 ) + 0.14 ), 0.0, 1.0 ); }\n"
"void main( void )\n"
"{\n"
"	vec2 uv = gl_TexCoord[0].xy;\n"
"	vec3 c = texture2D( scene, uv ).rgb;\n"
"	vec2 vel = vec2( 0.0 );\n"
"	if( movimiento > 0.0 )\n"
"	{\n"
		// where this pixel was on the screen the frame before (only the camera moves: the world stays)
"		float d = texture2D( depthmap, centro( uv )).r;\n"
"		if( d >= 0.3 )\n"	// the weapon moves with the eye: not blurred
"		{\n"
"			vec3 p = d >= 0.9999 ? vec3(( uv * 2.0 - 1.0 ) * tanfov, -1.0 ) * 100000.0 : posicion( uv );\n"
"			vec3 P = eye + vr * p.x + vu * p.y - vf * p.z;\n"
"			vec3 v = P - peye;\n"
"			float z = dot( v, pvf );\n"
"			if( z > 1.0 )\n"
"			{\n"
"				vec2 q = vec2( dot( v, pvr ), dot( v, pvu )) / ( z * tanfov ) * 0.5 + 0.5;\n"
"				vel = ( uv - q ) * movimiento;\n"
"				float l = length( vel );\n"
"				if( l > 0.04 ) vel *= 0.04 / l;\n"
"			}\n"
"		}\n"
"	}\n"
"	float mb = length( vel / texel );\n"
"	if( mb > 1.0 )\n"
"	{\n"
"		vec3 s = vec3( 0.0 );\n"
"		vec4 r = vec4( 0.0 );\n"
"		float n = 0.0;\n"
"		for( int i = 0; i < 8; i++ )\n"
"		{\n"
"			vec2 q = uv - vel * (( float( i ) + 0.5 ) / 8.0 - 0.5 );\n"
"			if( texture2D( depthmap, centro( q )).r < 0.3 ) continue;\n"	// not the weapon: it smeared over the world around it
"			s += texture2D( scene, q ).rgb;\n"
"			r += texture2D( ssrmap, q );\n"
"			n += 1.0;\n"
"		}\n"
"		if( n < 0.5 ) { s = c; r = texture2D( ssrmap, uv ); n = 1.0; }\n"
"		c = s / n;\n"
"		vec4 rf = r / n * ssrk;\n"
"		rf.rgb /= 1.0 + max( max( rf.r, max( rf.g, rf.b )) - 1.0, 0.0 );\n"
"		c = c * ( 1.0 - rf.a ) + rf.rgb;\n"
"	}\n"
"	else\n"
"	{\n"
"	if( sharpen > 0.0 )\n"
"	{\n"
"		vec3 v = texture2D( scene, uv + vec2( texel.x, 0.0 )).rgb + texture2D( scene, uv - vec2( texel.x, 0.0 )).rgb\n"
"			+ texture2D( scene, uv + vec2( 0.0, texel.y )).rgb + texture2D( scene, uv - vec2( 0.0, texel.y )).rgb;\n"
"		c = clamp( c + ( c - v * 0.25 ) * sharpen, 0.0, 1.0 );\n"
"	}\n"
"	vec4 rf = texture2D( ssrmap, uv ) * ssrk;\n"
"	rf.rgb /= 1.0 + max( max( rf.r, max( rf.g, rf.b )) - 1.0, 0.0 );\n"	// the lamps of the mirrors back under white
"	c = c * ( 1.0 - rf.a ) + rf.rgb;\n"
"	}\n"
"	float ao = mix( 1.0, texture2D( aomap, uv ).r, aok );\n"
"	vec3 b = texture2D( bloom, uv ).rgb * bloomk + texture2D( rayos, uv ).rgb * rayosk;\n"
"	if( lentek > 0.0 ) b += lente( uv ) * lentek;\n"
"	if( debug == 1 ) { gl_FragColor = vec4( vec3( ao ), 1.0 ); return; }\n"
"	if( debug == 2 ) { gl_FragColor = vec4( pow( b, vec3( 1.0 / 2.2 )), 1.0 ); return; }\n"
"	if( debug == 3 || debug == 5 || debug >= 7 ) { vec4 rd = texture2D( ssrmap, uv ) * ssrk; gl_FragColor = vec4( rd.rgb + vec3( 0.0, 0.0, 0.25 * rd.a ), 1.0 ); return; }\n"
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
"	vec3 o = pow( clamp( lin, 0.0, 1.0 ), vec3( 1.0 / 2.2 ));\n"
	// lift the darks and the middle: black stays black and white stays white (the filmic curve and the
	// occlusion left the half lit places too dark)
"	vec3 io = 1.0 - o;\n"
"	o += sombras * 2.0 * o * io * io * io;\n"
"	gl_FragColor = vec4( clamp( o, 0.0, 1.0 ), 1.0 );\n"
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
		pglGenerateMipmap = R_PostProc( "glGenerateMipmap" );
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
		post.p_mprof = R_PostShader( "mprof", fs_mprof );
		post.p_fxaa = R_PostShader( "fxaa", fs_fxaa );
		post.p_rayfuente = R_PostShader( "rayfuente", fs_rayfuente );
		post.p_rayos = R_PostShader( "rayos", fs_rayos );
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
		|| !R_PostTarget( &post.rt[RT_SSR], w, h, fmt, type, 0 )	// full size: half, the mirrors went blurry far away
		|| !R_PostTarget( &post.rt[RT_SSR2], w, h, fmt, type, 0 )
		|| !R_PostTarget( &post.rt[RT_FINAL], w, h, GL_RGBA8, GL_UNSIGNED_BYTE, 0 )	// the final image, for the antialiasing
		|| !R_PostTarget( &post.rt[RT_RAY1], w / 4, h / 4, fmt, type, 0 )	// light shafts
		|| !R_PostTarget( &post.rt[RT_RAY2], w / 4, h / 4, fmt, type, 0 ))
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
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_fuerza );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_key );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_min );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_max );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_speed );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_bloom );
	gEngfuncs.Cvar_RegisterVariable( &r_hdr_bloom_threshold );
	gEngfuncs.Cvar_RegisterVariable( &r_post_saturation );
	gEngfuncs.Cvar_RegisterVariable( &r_post_sharpen );
	gEngfuncs.Cvar_RegisterVariable( &r_post_vignette );
	gEngfuncs.Cvar_RegisterVariable( &r_post_sombras );
	gEngfuncs.Cvar_RegisterVariable( &r_post_movimiento );
	gEngfuncs.Cvar_RegisterVariable( &r_post_fxaa );
	gEngfuncs.Cvar_RegisterVariable( &r_post_rayos );
	gEngfuncs.Cvar_RegisterVariable( &r_post_lente );
	gEngfuncs.Cvar_RegisterVariable( &r_ssr );
	gEngfuncs.Cvar_RegisterVariable( &r_ssr_strength );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_luz );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_luz_ref );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_luz_curva );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_luces );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_halo );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_halo_fuerza );
	gEngfuncs.Cvar_RegisterVariable( &r_reflect_bloom );
	gEngfuncs.Cvar_RegisterVariable( &r_post_debug );
	gEngfuncs.Cvar_RegisterVariable( &r_lupa );
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

#define MIRRORS	10	// gl_bump.c MAX_PLANES

static const char *R_PostName( const char *fmt, int i )
{
	static char buf[4][32];
	static int n;
	n = ( n + 1 ) & 3;
	Q_snprintf( buf[n], sizeof( buf[n] ), fmt, i );
	return buf[n];
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
R_PostMirrorDepth

custom: the depth of mirror view into the alpha of its image (gl_bump.c, after drawing it)
==============
*/
void R_PostMirrorDepth( GLuint fbo, GLuint depth, int w, int h, float zfar )
{
	if( !post.ok || !post.p_mprof )
		return;
	pglMatrixMode( GL_PROJECTION ); pglPushMatrix(); pglLoadIdentity();
	pglMatrixMode( GL_MODELVIEW ); pglPushMatrix(); pglLoadIdentity();
	pglDisable( GL_DEPTH_TEST );
	pglDepthMask( GL_FALSE );
	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_CULL_FACE );
	pglDisable( GL_CLIP_PLANE0 );
	pglBindFramebuffer( GL_FRAMEBUFFER, fbo );
	pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );	// read while it is not attached
	pglViewport( 0, 0, w, h );
	pglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE );
	R_PostBind( 0, depth );
	pglUseProgramObjectARB( post.p_mprof );
	R_PostUniforms1i( post.p_mprof, "dt", 0 );
	R_PostUniform1f( post.p_mprof, "zf", zfar );
	pglBegin( GL_QUADS );
	pglTexCoord2f( 0, 0 ); pglVertex2f( -1, -1 );
	pglTexCoord2f( 1, 0 ); pglVertex2f( 1, -1 );
	pglTexCoord2f( 1, 1 ); pglVertex2f( 1, 1 );
	pglTexCoord2f( 0, 1 ); pglVertex2f( -1, 1 );
	pglEnd();
	pglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglMatrixMode( GL_PROJECTION ); pglPopMatrix();
	pglMatrixMode( GL_MODELVIEW ); pglPopMatrix();
	pglEnable( GL_DEPTH_TEST );
	pglDepthMask( GL_TRUE );
	R_PostRestoreState();
}

/*
==============
R_PostMirrorBounce

custom: the second bounce. The image of mirror k (fbo, its colour copied to scene, its depth)
gets the reflection of mirror j in it, as the screen gets the first ones: the same shader with
the camera of mirror k in place of the eye and only mirror j
==============
*/
qboolean R_PostMirrorBounce( GLuint fbo, GLuint scene, GLuint depth, int w, int h, const vec3_t eye, const vec3_t vf, const vec3_t vr, const vec3_t vu,
	float tx, float ty, float zfar, const vec4_t plane, GLuint tex, GLuint tdep, float tzfar, const vec3_t pe, const vec3_t pf, const vec3_t pr, const vec3_t pu, float blur )
{
	GLint l;

	if( !post.ok || !post.p_cube )
		return false;

	pglMatrixMode( GL_PROJECTION ); pglPushMatrix(); pglLoadIdentity();
	pglMatrixMode( GL_MODELVIEW ); pglPushMatrix(); pglLoadIdentity();
	pglDisable( GL_DEPTH_TEST );
	pglDepthMask( GL_FALSE );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_CULL_FACE );
	pglDisable( GL_CLIP_PLANE0 );
	pglColor4f( 1, 1, 1, 1 );

	R_PostBind( 0, depth );
	R_PostBind( 1, scene );
	R_PostBind( 4, tex );
	R_PostBind( 8, tdep );
	pglUseProgramObjectARB( post.p_cube );
	for( int k = 0; k < MIRRORS; k++ )
		R_PostUniforms1i( post.p_cube, R_PostName( "plano%i", k ), 4 );
	R_PostUniforms1i( post.p_cube, "depthmap", 0 );
	R_PostUniforms1i( post.p_cube, "scene", 1 );
	R_PostUniforms1i( post.p_cube, "cubecolor", 0 );
	R_PostUniforms1i( post.p_cube, "cubedepth", 0 );
	R_PostUniform1f( post.p_cube, "znear", 4.0f );
	R_PostUniform1f( post.p_cube, "zfar", zfar );
	R_PostUniform2f( post.p_cube, "tanfov", tx, ty );
	R_PostUniform2f( post.p_cube, "texel", 1.0f / w, 1.0f / h );
	R_PostUniform2f( post.p_cube, "dtexel", 1.0f / w, 1.0f / h );
	R_PostUniform1f( post.p_cube, "strength", r_ssr_strength.value );
	R_PostUniform1f( post.p_cube, "luz", bound( 0.0f, r_reflect_luz.value, 1.0f ));
	R_PostUniform1f( post.p_cube, "luzref", Q_max( r_reflect_luz_ref.value, 0.01f ));
	R_PostUniform1f( post.p_cube, "luzcurva", Q_max( r_reflect_luz_curva.value, 0.1f ));
	R_PostUniform1f( post.p_cube, "luces", Q_max( r_reflect_luces.value, 0.0f ));
	R_PostUniform1f( post.p_cube, "halo", Q_max( r_reflect_halo.value, 0.0f ));
		R_PostUniform1f( post.p_cube, "halok", Q_max( r_reflect_halo_fuerza.value, 0.0f ));
	R_PostUniform1f( post.p_cube, "dbg", r_post_debug.value == 6.0f ? 6.0f : 0.0f );	// 6: the second bounce in magenta
	R_PostUniform3f( post.p_cube, "eye", eye );
	R_PostUniform3f( post.p_cube, "vf", vf );
	R_PostUniform3f( post.p_cube, "vr", vr );
	R_PostUniform3f( post.p_cube, "vu", vu );
	R_PostUniform1f( post.p_cube, "nplanos", 1.0f );
	R_PostUniform1f( post.p_cube, "cubo", 0.0f );
	R_PostUniform2f( post.p_cube, "ptan", tx, ty );
	if(( l = pglGetUniformLocationARB( post.p_cube, "pn" )) >= 0 ) pglUniform4fvARB( l, 1, plane );
	R_PostUniform3f( post.p_cube, "pe[0]", pe );
	R_PostUniform3f( post.p_cube, "pf[0]", pf );
	R_PostUniform3f( post.p_cube, "pr[0]", pr );
	R_PostUniform3f( post.p_cube, "pu[0]", pu );
	R_PostUniform1f( post.p_cube, "pblur[0]", blur );
	R_PostUniform1f( post.p_cube, "pfade[0]", 1.0f );

	// over the image of mirror k, as the final pass puts them over the view (its alpha kept)
	pglBindFramebuffer( GL_FRAMEBUFFER, fbo );
	pglViewport( 0, 0, w, h );
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	pglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE );
	pglUseProgramObjectARB( post.p_cube );
	pglBegin( GL_QUADS );
	pglTexCoord2f( 0, 0 ); pglVertex2f( -1, -1 );
	pglTexCoord2f( 1, 0 ); pglVertex2f( 1, -1 );
	pglTexCoord2f( 1, 1 ); pglVertex2f( 1, 1 );
	pglTexCoord2f( 0, 1 ); pglVertex2f( -1, 1 );
	pglEnd();
	pglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	pglDisable( GL_BLEND );

	pglActiveTextureARB( GL_TEXTURE0_ARB + 8 ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB + 4 ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE1_ARB ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglMatrixMode( GL_PROJECTION ); pglPopMatrix();
	pglMatrixMode( GL_MODELVIEW ); pglPopMatrix();
	pglEnable( GL_DEPTH_TEST );
	pglDepthMask( GL_TRUE );
	R_PostRestoreState();
	return true;
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
		R_PostUniform1f( post.p_cube, "luz", bound( 0.0f, r_reflect_luz.value, 1.0f ));
		R_PostUniform1f( post.p_cube, "luzref", Q_max( r_reflect_luz_ref.value, 0.01f ));
		R_PostUniform1f( post.p_cube, "luzcurva", Q_max( r_reflect_luz_curva.value, 0.1f ));
		R_PostUniform1f( post.p_cube, "luces", Q_max( r_reflect_luces.value, 0.0f ));
		R_PostUniform1f( post.p_cube, "halo", Q_max( r_reflect_halo.value, 0.0f ));
		R_PostUniform1f( post.p_cube, "halok", Q_max( r_reflect_halo_fuerza.value, 0.0f ));
		R_PostUniform1f( post.p_cube, "dbg", r_post_debug.value == 6.0f ? 0.0f : r_post_debug.value );
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
			GLuint ptex[MIRRORS], pdep[MIRRORS];
			float pzf[MIRRORS], pbl[MIRRORS], pfd[MIRRORS];
			vec4_t pn[MIRRORS];
			vec3_t pe[MIRRORS], pf[MIRRORS], pr[MIRRORS], pu[MIRRORS];
			float ptx = 1.0f, pty = 1.0f;
			int np = R_BumpMirrors( ptex, pdep, pzf, pn, pe, pf, pr, pu, pbl, pfd, &ptx, &pty );
			GLint l;

			// mirror k on unit 4 + k (its depth in the alpha)
			for( i = 0; i < MIRRORS; i++ )
			{
				if( i < np )
				{
					R_PostBind( 4 + i, ptex[i] );
					R_PostUniform1f( post.p_cube, R_PostName( "pblur[%i]", i ), pbl[i] );	// one by one: in one of the tables the list call is the single one
					R_PostUniform1f( post.p_cube, R_PostName( "pfade[%i]", i ), pfd[i] );
				}
				R_PostUniforms1i( post.p_cube, R_PostName( "plano%i", i ), 4 + i );
			}
			R_PostUniform1f( post.p_cube, "nplanos", np );
			R_PostUniform1f( post.p_cube, "cubo", R_BumpCubeOn( ) ? 1.0f : 0.0f );
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
		R_PostUniform1f( post.p_ssr, "luz", bound( 0.0f, r_reflect_luz.value, 1.0f ));
		R_PostUniform1f( post.p_ssr, "luzref", Q_max( r_reflect_luz_ref.value, 0.01f ));
		R_PostUniform1f( post.p_ssr, "luzcurva", Q_max( r_reflect_luz_curva.value, 0.1f ));
		R_PostUniform1f( post.p_ssr, "luces", Q_max( r_reflect_luces.value, 0.0f ));
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
			float objetivo = bound( r_hdr_min.value, pow( r_hdr_key.value / Q_max( media, 0.0001f ), bound( 0.0f, r_hdr_fuerza.value, 1.0f )), r_hdr_max.value );

			dt = gp_host->realtime - post.lasttime;
			post.lasttime = gp_host->realtime;
			dt = bound( 0.0, dt, 0.25 );
			post.exposure += ( objetivo - post.exposure ) * ( 1.0f - exp( -dt * r_hdr_speed.value ));
			if( !isfinite( post.exposure )) post.exposure = 1.0f;
			if( r_post_debug.value == 4 )
				gEngfuncs.Con_NPrintf( 17, "hdr: average %.4f, target %.2f, exposure %.2f", media, objetivo, post.exposure );
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
	R_PostBind( 1, rt[RT_SSR].tex );
	R_PostUniforms1i( post.p_bright, "ssrmap", 1 );
	R_PostUniform1f( post.p_bright, "ssrk", hecho ? 1.0f : 0.0f );
	R_PostUniform1f( post.p_bright, "rbloom", Q_max( r_reflect_bloom.value, 0.0f ));
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

	// 3b. light shafts
	{
		float rk = Q_max( r_post_rayos.value, 0.0f );
		vec3_t sun, rel;
		float sx = 0.5f, sy = 0.5f, solk = 0.0f;

		if( rk > 0.0f && post.p_rayfuente && post.p_rayos )
		{
			const movevars_t *mv = gp_movevars;

			VectorSet( sun, -mv->skyvec[0], -mv->skyvec[1], -mv->skyvec[2] );
			if( VectorNormalizeLength( sun ) < 0.01f )
				VectorSet( sun, 0.3f, 0.2f, 0.93f );
			rel[0] = DotProduct( sun, post.vr );
			rel[1] = DotProduct( sun, post.vu );
			rel[2] = DotProduct( sun, post.vf );
			if( rel[2] > 0.05f )
			{
				sx = rel[0] / ( rel[2] * tx ) * 0.5f + 0.5f;
				sy = rel[1] / ( rel[2] * ty ) * 0.5f + 0.5f;
				solk = bound( 0.0f, ( rel[2] - 0.05f ) * 3.0f, 1.0f );
			}

			R_PostBind( 0, post.depth );
			R_PostBind( 1, rt[RT_SCENE].tex );
			pglUseProgramObjectARB( post.p_rayfuente );
			R_PostUniforms1i( post.p_rayfuente, "depthmap", 0 );
			R_PostUniforms1i( post.p_rayfuente, "scene", 1 );
			R_PostUniform2f( post.p_rayfuente, "texel", 1.0f / w, 1.0f / h );
			R_PostUniform1f( post.p_rayfuente, "exposure", post.exposure * r_hdr_exposure.value );
			R_PostPass( &rt[RT_RAY1], post.p_rayfuente );

			R_PostBind( 0, rt[RT_RAY1].tex );
			pglUseProgramObjectARB( post.p_rayos );
			R_PostUniforms1i( post.p_rayos, "src", 0 );
			R_PostUniform2f( post.p_rayos, "sol", sx, sy );
			R_PostUniform1f( post.p_rayos, "solk", solk );
			R_PostPass( &rt[RT_RAY2], post.p_rayos );
			pglActiveTextureARB( GL_TEXTURE1_ARB ); pglBindTexture( GL_TEXTURE_2D, 0 );
		}
		post.rayosk = rk;
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
	R_PostBind( 5, rt[RT_RAY2].tex );
	R_PostUniforms1i( post.p_final, "rayos", 5 );
	R_PostUniform1f( post.p_final, "rayosk", post.rayosk );
	{
		// the zoom: the vertical view against the one of default_fov (given for 4:3), so 16:9 counts as no zoom
		static cvar_t *default_fov = NULL;
		float def = 90.0f, zoom, k = 0.0f;

		if( !default_fov )
			default_fov = gEngfuncs.pfnGetCvarPointer( "default_fov" );
		if( default_fov && default_fov->value > 1.0f )
			def = default_fov->value;
		zoom = tan( def * M_PI_F / 360.0f ) * 0.75f / Q_max( post.tany, 0.001f );
		if( r_post_lente.value >= 9.0f )
			k = 1.0f;	// 9: always, as with the most zoom, for tests
		else if( r_post_lente.value > 0.0f )
			k = r_post_lente.value * bound( 0.0f, ( zoom - 1.3f ) / 1.5f, 1.0f );
		R_PostUniform1f( post.p_final, "lentek", k );
	}
	R_PostUniform1f( post.p_final, "aok", r_ssao.value ? 1.0f : 0.0f );
	R_PostUniform1f( post.p_final, "sat", r_post_saturation.value );
	R_PostUniform1f( post.p_final, "sharpen", r_post_sharpen.value );
	R_PostUniform1f( post.p_final, "vignette", r_post_vignette.value );
	R_PostUniform1f( post.p_final, "sombras", Q_max( r_post_sombras.value, 0.0f ));
	R_PostUniform1f( post.p_final, "hdr", r_hdr.value ? 1.0f : 0.0f );
	R_PostUniforms1i( post.p_final, "debug", (int)r_post_debug.value );
	{
		// motion blur: the camera of the frame before; the shutter open a part of a 60 fps frame,
		// whatever the frames really are. Not after a jump of the camera (spawn, spectator, teleport)
		double now = gp_host->realtime, dt = now - post.ptime;
		float k = 0.0f;

		if( r_post_movimiento.value > 0.0f && post.pvalid && dt > 0.0 && dt < 0.1
			&& VectorDistance( post.eye, post.peye ) < 64.0f && DotProduct( post.vf, post.pvf ) > 0.7f )
			k = Q_min( r_post_movimiento.value * ( 1.0 / 60.0 ) / dt, 2.0f );
		R_PostBind( 4, post.depth );
		R_PostUniforms1i( post.p_final, "depthmap", 4 );
		R_PostUniform1f( post.p_final, "znear", 4.0f );
		R_PostUniform1f( post.p_final, "zfar", zfar );
		R_PostUniform2f( post.p_final, "tanfov", tx, ty );
		R_PostUniform2f( post.p_final, "dtexel", 1.0f / w, 1.0f / h );
		R_PostUniform3f( post.p_final, "eye", post.eye );
		R_PostUniform3f( post.p_final, "vf", post.vf );
		R_PostUniform3f( post.p_final, "vr", post.vr );
		R_PostUniform3f( post.p_final, "vu", post.vu );
		R_PostUniform3f( post.p_final, "peye", post.peye );
		R_PostUniform3f( post.p_final, "pvf", post.pvf );
		R_PostUniform3f( post.p_final, "pvr", post.pvr );
		R_PostUniform3f( post.p_final, "pvu", post.pvu );
		R_PostUniform1f( post.p_final, "movimiento", k );
		VectorCopy( post.eye, post.peye );
		VectorCopy( post.vf, post.pvf );
		VectorCopy( post.vr, post.pvr );
		VectorCopy( post.vu, post.pvu );
		post.ptime = now;
		post.pvalid = true;
	}
	{
		// with the antialiasing, the final image goes through one more pass on its way to the screen
		qboolean fxaa = r_post_fxaa.value && post.p_fxaa && r_post_debug.value == 0.0f;

		R_PostPass( fxaa ? &rt[RT_FINAL] : NULL, post.p_final );
		pglActiveTextureARB( GL_TEXTURE0_ARB + 4 ); pglBindTexture( GL_TEXTURE_2D, 0 );
		pglActiveTextureARB( GL_TEXTURE0_ARB + 5 ); pglBindTexture( GL_TEXTURE_2D, 0 );
		if( fxaa )
		{
			R_PostBind( 0, rt[RT_FINAL].tex );
			pglUseProgramObjectARB( post.p_fxaa );
			R_PostUniforms1i( post.p_fxaa, "src", 0 );
			R_PostUniform2f( post.p_fxaa, "texel", 1.0f / w, 1.0f / h );
			R_PostPass( NULL, post.p_fxaa );
		}
	}

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

// custom: this frame goes through the post processing (the water draws with what is under it)
qboolean R_PostActive( void )
{
	return post.active;
}

qboolean R_PostRunning( void )
{
	return r_post.value && post.ok;
}

/*
==============
R_Lupa

custom: r_lupa, the 3D view just drawn copied and drawn again r_lupa times
bigger about the middle (to place the sights with ads_editar). What is drawn
after, the HUD, is not
==============
*/
void R_Lupa( void )
{
	static GLuint	tex;
	float		z = r_lupa.value, m;
	int		x, y, w, h;

	if( z <= 1.0f || !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
		return;

	x = RI.rvp.viewport[0];
	w = RI.rvp.viewport[2];
	h = RI.rvp.viewport[3];
	y = gpGlobals->height - ( RI.rvp.viewport[1] + h );
	if( w <= 0 || h <= 0 )
		return;

	if( !tex )
		pglGenTextures( 1, &tex );

	pglActiveTextureARB( GL_TEXTURE0_ARB );
	pglBindTexture( GL_TEXTURE_2D, tex );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	pglCopyTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, x, y, w, h, 0 );

	pglViewport( x, y, w, h );
	pglMatrixMode( GL_PROJECTION );
	pglPushMatrix();
	pglLoadIdentity();
	pglMatrixMode( GL_MODELVIEW );
	pglPushMatrix();
	pglLoadIdentity();

	pglDisable( GL_DEPTH_TEST );
	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	GL_Cull( GL_NONE );
	pglEnable( GL_TEXTURE_2D );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );

	m = 0.5f / z;
	pglBegin( GL_QUADS );
	pglTexCoord2f( 0.5f - m, 0.5f - m ); pglVertex2f( -1, -1 );
	pglTexCoord2f( 0.5f + m, 0.5f - m ); pglVertex2f( 1, -1 );
	pglTexCoord2f( 0.5f + m, 0.5f + m ); pglVertex2f( 1, 1 );
	pglTexCoord2f( 0.5f - m, 0.5f + m ); pglVertex2f( -1, 1 );
	pglEnd();

	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglEnable( GL_DEPTH_TEST );
	pglMatrixMode( GL_PROJECTION );
	pglPopMatrix();
	pglMatrixMode( GL_MODELVIEW );
	pglPopMatrix();
	R_PostRestoreState();
}
