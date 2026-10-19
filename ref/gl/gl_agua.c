/*
gl_agua.c - water with refraction, waves, depth colour and reflections (custom)
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
The water of the world (the textures that start with '!') and the func_water are drawn after the rest of
the view, with a shader that reads a copy of the view drawn so far (colour and depth). Two cases, apart:

From the air: what is under the surface, bent by waves made in the shader where there is water enough
to bend it, and taking the colour of the water the deeper it is. It asks gl_bump.c for a mirror on its
plane, and when it has one it reflects it itself, moved by the same waves: fully at a grazing look, less
and less as the look goes down, and nothing from r_agua_reflejo_angulo up (only what is under it seen).
Without one, the reflection is left to gl_post.c (the cube): the water writes in the alpha how much it
reflects (Fresnel) and marks itself as a mirror.

From inside: what is above, bent by the waves, nearly without colour. So that what is above is
drawn at all, R_MarkLeaves adds the leaves seen from just above the surface (R_AguaEncima): with those of
the eye alone the bridge was missing and other parts of the map were seen through the hole.

Only in the main view through the post processing; the mirrors and the cube draw it as always.
*/

CVAR_DEFINE_AUTO( r_agua, "1", FCVAR_ARCHIVE, "custom: water with refraction, waves and reflections (0 as always)" );
CVAR_DEFINE_AUTO( r_agua_ondas, "1", FCVAR_ARCHIVE, "custom: how much the waves bend what is seen through the water and its reflection" );
CVAR_DEFINE_AUTO( r_agua_claridad, "1500", FCVAR_ARCHIVE, "custom: how deep (units) one sees through the water before it takes its own colour" );
CVAR_DEFINE_AUTO( r_agua_textura, "0", FCVAR_ARCHIVE, "custom: how much of the pattern of the water texture is drawn on it (0 only its colour, 1 all)" );
CVAR_DEFINE_AUTO( r_agua_reflejo, "1", FCVAR_ARCHIVE, "custom: how much the water reflects (times Fresnel)" );
CVAR_DEFINE_AUTO( r_agua_reflejo_angulo, "50", FCVAR_ARCHIVE, "custom: the look over the water (degrees from its surface) from which it no longer reflects its mirror: under it, the more grazing the more" );
CVAR_DEFINE_AUTO( r_agua_reflejo_ondas, "1", FCVAR_ARCHIVE, "custom: how much the waves move the reflection of the mirror" );

static const char *vs_agua =
"uniform mat4 objeto;\n"
"varying vec3 wpos;\n"
"void main( void )\n"
"{\n"
"	wpos = ( objeto * gl_Vertex ).xyz;\n"
"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"	gl_Position = ftransform();\n"
"}\n";

static const char *fs_agua =
"uniform sampler2D agua, fondo, fondoz;\n"
"uniform vec2 pantalla;\n"
"uniform vec3 eye, nplano;\n"
"uniform float tiempo, znear, zfar, claridad, ondas, refl, dibujok, dentro;\n"
// the mirror of its plane (gl_bump.c), if it has one: as gl_post.c reads them
"uniform sampler2D espejo;\n"
"uniform vec4 epn;\n"
"uniform vec3 epe, epf, epr, epu;\n"
"uniform vec2 eptan;\n"
"uniform float hayespejo, efade, eseno, eondas;\n"
"varying vec3 wpos;\n"
"vec3 espejoPunto( vec2 c ) { vec2 q = ( c * 2.0 - 1.0 ) * eptan; return epe + epf + epr * q.x + epu * q.y; }\n"
"vec2 espejoUV( vec3 P ) { vec3 v = P - epe; return vec2( dot( v, epr ), dot( v, epu )) / ( dot( v, epf ) * eptan ) * 0.5 + 0.5; }\n"
// the image can be of a frame before, drawn from where the eye was: walked along the ray of now with its
// depth (in the alpha), as gl_post.c
"vec2 reproyecta( vec3 P )\n"
"{\n"
"	vec2 c = espejoUV( P );\n"
"	vec3 en = eye - 2.0 * ( dot( eye, epn.xyz ) - epn.w ) * epn.xyz;\n"
"	if( dot( en - epe, en - epe ) < 0.01 ) return c;\n"
"	vec3 dn = normalize( P - en );\n"
"	for( int i = 0; i < 3; i++ )\n"
"	{\n"
"		vec3 dir = normalize( espejoPunto( c ) - epe );\n"
"		float z = texture2D( espejo, c, -16.0 ).a;\n"
"		vec3 Q = epe + dir * ( z / max( dot( dir, epf ), 0.05 ));\n"
"		c = espejoUV( en + dn * max( dot( Q - en, dn ), 1.0 ));\n"
"	}\n"
"	return c;\n"
"}\n"
"float lineal( float d ) { float z = d * 2.0 - 1.0; return 2.0 * znear * zfar / ( zfar + znear - z * ( zfar - znear )); }\n"
// the slope of a few waves crossing (each one sin of its direction): what bends the normal
"vec2 ola( vec2 p, float t )\n"
"{\n"
"	vec2 g = vec2( 0.0 );\n"
"	vec2 d;\n"
"	d = vec2( 0.8, 0.6 );   g += d * cos( dot( p, d ) * 0.050 + t * 1.10 ) * 0.050;\n"
"	d = vec2( -0.4, 0.92 ); g += d * cos( dot( p, d ) * 0.090 - t * 1.60 ) * 0.035;\n"
"	d = vec2( 0.95, -0.3 ); g += d * cos( dot( p, d ) * 0.170 + t * 2.30 ) * 0.020;\n"
"	d = vec2( -0.7, -0.7 ); g += d * cos( dot( p, d ) * 0.310 - t * 3.10 ) * 0.012;\n"
"	return g;\n"
"}\n"
"void main( void )\n"
"{\n"
"	vec2 suv = gl_FragCoord.xy * pantalla;\n"
"	vec3 V = normalize( eye - wpos );\n"
"	vec3 N = nplano;\n"
"	if( dot( V, N ) < 0.0 ) N = -N;\n"	// the side of the face that looks at the eye
"	vec3 T = normalize( cross( N, abs( N.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 )));\n"
"	vec3 B = cross( N, T );\n"
"	vec2 g = ola( vec2( dot( wpos, T ), dot( wpos, B )), tiempo ) * ondas;\n"
"	vec3 Nw = normalize( N - T * g.x - B * g.y );\n"
"	float cv = max( dot( Nw, V ), 0.0 );\n"
"	float zw = lineal( gl_FragCoord.z );\n"
"	float lejos = max( zw / 64.0, 1.0 );\n"
	// the colour of the water: its texture averaged (its last mip level, one texel: with a 4x4 one its blotches were seen); its pattern only if asked
"	vec3 tinte = texture2D( agua, gl_TexCoord[0].xy, 16.0 ).rgb;\n"
"	vec3 dibujo = ( texture2D( agua, gl_TexCoord[0].xy ).rgb - tinte ) * dibujok;\n"
"	vec3 hondo = tinte * 0.6;\n"
"	if( dentro < 0.5 )\n"
"	{\n"
		// from the air. Bent only where there is water under it to bend the view: at the shore, none
		// (the wall that goes into the water was pulled away from it)
"		float zb = lineal( texture2D( fondoz, suv ).r );\n"
"		vec2 ruv = suv + g * 0.6 * clamp(( zb - zw ) / 48.0, 0.0, 1.0 ) / lejos;\n"
"		float zr = lineal( texture2D( fondoz, ruv ).r );\n"
"		if( zr < zw ) { ruv = suv; zr = zb; }\n"	// not what is in front of the water (a leg) pulled into it
		// how much water the light crosses: towards the bottom more than along the look (the ray bends
		// down entering the water), so at a grazing look the bottom does not vanish at once
"		float espesor = max( zr - zw, 0.0 ) * mix( 1.0, abs( dot( V, N )), 0.7 );\n"
"		vec3 c = texture2D( fondo, ruv ).rgb * mix( vec3( 1.0 ), tinte * 1.8, 0.4 );\n"
"		c = mix( c, hondo, 1.0 - exp( -espesor / max( claridad, 1.0 ))) + dibujo;\n"
"		if( hayespejo > 0.5 )\n"
"		{\n"
			// its mirror, moved by the waves (less far away, as the refraction). How much: all of it at a
			// grazing look, nothing from the angle asked up (the sine of the look over the flat surface)
"			vec2 euv = reproyecta( wpos ) + g * 0.2 * eondas / lejos;\n"
"			vec2 borde = min( euv, 1.0 - euv );\n"
"			float dentrov = clamp( min( borde.x, borde.y ) * 20.0, 0.0, 1.0 );\n"
"			float sube = clamp( max( dot( V, N ), 0.0 ) / max( eseno, 0.01 ), 0.0, 1.0 );\n"
"			float k = refl * efade * dentrov * pow( 1.0 - sube, 1.5 );\n"
"			vec3 r = texture2D( espejo, clamp( euv, 0.0, 1.0 )).rgb;\n"
			// all done here: nothing more for gl_post.c to reflect on it
"			gl_FragColor = vec4( mix( c, r, clamp( k, 0.0, 1.0 )), 1.0 );\n"
"		}\n"
"		else\n"
"		{\n"
"			float k = clamp(( 0.02 + 0.98 * pow( 1.0 - cv, 5.0 )) * refl, 0.0, 1.0 );\n"
			// for gl_post.c, as gl_bump.c: 1 minus how much it reflects, no roughness, and 32 (may be a mirror)
"			gl_FragColor = vec4( c, 1.0 - k + ( k > 0.0 ? 32.0 : 0.0 ));\n"
"		}\n"
"	}\n"
"	else\n"
"	{\n"
		// from inside: what is above, bent by the waves as from the air, nearly without colour (with the
		// window of Snell, the rest of the surface was a ceiling of the colour of the water)
"		vec2 ruv = suv + g * 0.6 / lejos;\n"
"		float d = texture2D( fondoz, ruv ).r;\n"
"		if( lineal( d ) < zw ) { ruv = suv; d = texture2D( fondoz, suv ).r; }\n"
"		vec3 aire = texture2D( fondo, ruv ).rgb;\n"
		// nothing drawn there (out of what the engine sees, the view not cleared): the light of the day
"		if( d >= 0.99999 ) aire = tinte * 1.6;\n"
"		gl_FragColor = vec4( aire * mix( vec3( 1.0 ), tinte * 1.8, 0.15 ) + dibujo, 1.0 );\n"
"	}\n"
"}\n";

static struct
{
	qboolean	tried, ok;
	GLhandleARB	prog;
	GLint		u_dentro, u_dibujo, u_objeto, u_agua, u_fondo, u_fondoz, u_pantalla, u_eye, u_nplano, u_tiempo, u_znear, u_zfar, u_claridad, u_ondas, u_refl;
	GLint		u_espejo, u_epn, u_epe, u_epf, u_epr, u_epu, u_eptan, u_hayespejo, u_efade, u_eseno, u_eondas;
	GLuint		color, depth;
	int		w, h;
	cl_entity_t	*ents[64];	// func_water of this view, drawn with the water of the world
	int		nents;
} agua;

void R_InitAgua( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_agua );
	gEngfuncs.Cvar_RegisterVariable( &r_agua_ondas );
	gEngfuncs.Cvar_RegisterVariable( &r_agua_claridad );
	gEngfuncs.Cvar_RegisterVariable( &r_agua_reflejo );
	gEngfuncs.Cvar_RegisterVariable( &r_agua_reflejo_angulo );
	gEngfuncs.Cvar_RegisterVariable( &r_agua_reflejo_ondas );
	gEngfuncs.Cvar_RegisterVariable( &r_agua_textura );
	memset( &agua, 0, sizeof( agua ));
}

static GLhandleARB R_AguaShader( GLenum type, const char *src )
{
	GLhandleARB s = pglCreateShaderObjectARB( type );
	GLint ok = 0;
	char log[2048];

	pglShaderSourceARB( s, 1, &src, NULL );
	pglCompileShaderARB( s );
	pglGetObjectParameterivARB( s, GL_OBJECT_COMPILE_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( s, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_agua: shader: %s\n", log );
		return 0;
	}
	return s;
}

static qboolean R_AguaProgram( void )
{
	GLhandleARB v, f;
	GLint ok = 0;
	char log[2048];

	if( agua.tried )
		return agua.ok;
	agua.tried = true;

	if( !GL_Support( GL_SHADER_GLSL100_EXT ) || !GL_Support( GL_SHADER_OBJECTS_EXT ) || glConfig.max_texture_units < 4 )
		return false;
	if( !( v = R_AguaShader( GL_VERTEX_SHADER_ARB, vs_agua )) || !( f = R_AguaShader( GL_FRAGMENT_SHADER_ARB, fs_agua )))
		return false;
	agua.prog = pglCreateProgramObjectARB();
	pglAttachObjectARB( agua.prog, v );
	pglAttachObjectARB( agua.prog, f );
	pglLinkProgramARB( agua.prog );
	pglGetObjectParameterivARB( agua.prog, GL_OBJECT_LINK_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( agua.prog, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_agua: program: %s\n", log );
		return false;
	}
	agua.u_objeto = pglGetUniformLocationARB( agua.prog, "objeto" );
	agua.u_dibujo = pglGetUniformLocationARB( agua.prog, "dibujok" );
	agua.u_dentro = pglGetUniformLocationARB( agua.prog, "dentro" );
	agua.u_agua = pglGetUniformLocationARB( agua.prog, "agua" );
	agua.u_fondo = pglGetUniformLocationARB( agua.prog, "fondo" );
	agua.u_fondoz = pglGetUniformLocationARB( agua.prog, "fondoz" );
	agua.u_pantalla = pglGetUniformLocationARB( agua.prog, "pantalla" );
	agua.u_eye = pglGetUniformLocationARB( agua.prog, "eye" );
	agua.u_nplano = pglGetUniformLocationARB( agua.prog, "nplano" );
	agua.u_tiempo = pglGetUniformLocationARB( agua.prog, "tiempo" );
	agua.u_znear = pglGetUniformLocationARB( agua.prog, "znear" );
	agua.u_zfar = pglGetUniformLocationARB( agua.prog, "zfar" );
	agua.u_claridad = pglGetUniformLocationARB( agua.prog, "claridad" );
	agua.u_ondas = pglGetUniformLocationARB( agua.prog, "ondas" );
	agua.u_refl = pglGetUniformLocationARB( agua.prog, "refl" );
	agua.u_espejo = pglGetUniformLocationARB( agua.prog, "espejo" );
	agua.u_epn = pglGetUniformLocationARB( agua.prog, "epn" );
	agua.u_epe = pglGetUniformLocationARB( agua.prog, "epe" );
	agua.u_epf = pglGetUniformLocationARB( agua.prog, "epf" );
	agua.u_epr = pglGetUniformLocationARB( agua.prog, "epr" );
	agua.u_epu = pglGetUniformLocationARB( agua.prog, "epu" );
	agua.u_eptan = pglGetUniformLocationARB( agua.prog, "eptan" );
	agua.u_hayespejo = pglGetUniformLocationARB( agua.prog, "hayespejo" );
	agua.u_efade = pglGetUniformLocationARB( agua.prog, "efade" );
	agua.u_eseno = pglGetUniformLocationARB( agua.prog, "eseno" );
	agua.u_eondas = pglGetUniformLocationARB( agua.prog, "eondas" );
	agua.ok = true;
	return true;
}

/*
==============
R_AguaActiva

this view draws the water with the shader: the main one, through the post processing
==============
*/
qboolean R_AguaActiva( void )
{
	if( !r_agua.value || !R_PostActive( ))
		return false;
	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return false;
	if( R_BumpDrawingPlanar( ) || R_BumpDrawingDynamic( ))
		return false;
	return R_AguaProgram( );
}

/*
==============
R_AguaEncima

the eye in the water of the world: a point just above its surface, whose leaves R_MarkLeaves adds
==============
*/
qboolean R_AguaEncima( const vec3_t eye, vec3_t out )
{
	mleaf_t *leaf = gEngfuncs.Mod_PointInLeaf( eye, WORLDMODEL->nodes, WORLDMODEL );
	vec3_t p;

	if( !leaf || !IsLiquidContents( leaf->contents ) || !R_AguaActiva( ))
		return false;

	VectorCopy( eye, p );
	for( int i = 0; i < 256; i++ )
	{
		mleaf_t *l;

		p[2] += 8.0f;
		l = gEngfuncs.Mod_PointInLeaf( p, WORLDMODEL->nodes, WORLDMODEL );
		if( l->contents == leaf->contents )
			continue;
		if( l->contents == CONTENTS_SOLID )
			return false;	// a ceiling over the water: nothing above to see
		p[2] += 8.0f;
		VectorCopy( p, out );
		return true;
	}
	return false;
}

/*
==============
R_AguaEntidad

a brush entity of water (func_water): kept for R_AguaDibujar instead of drawn now
==============
*/
qboolean R_AguaEntidad( cl_entity_t *e )
{
	model_t *m = e->model;
	int i;

	if( !m || m->type != mod_brush || !m->nummodelsurfaces || !R_AguaActiva( ))
		return false;
	for( i = 0; i < m->nummodelsurfaces; i++ )
		if( FBitSet( m->surfaces[m->firstmodelsurface + i].flags, SURF_DRAWTURB ))
			break;
	if( i >= m->nummodelsurfaces )
		return false;	// no water in it
	for( i = 0; i < agua.nents; i++ )
		if( agua.ents[i] == e ) return true;
	if( agua.nents >= ARRAYSIZE( agua.ents ))
		return false;
	agua.ents[agua.nents++] = e;
	return true;
}

qboolean R_AguaPendiente( void )
{
	return agua.nents > 0;
}

static void R_AguaMatriz( void )
{
	float m[16];
	int i, j;

	for( i = 0; i < 4; i++ )
		for( j = 0; j < 4; j++ )
			m[i * 4 + j] = RI.objectMatrix[i][j];	// rows: given transposed
	pglUniformMatrix4fvARB( agua.u_objeto, 1, GL_TRUE, m );
}

static GLuint R_AguaTexture( GLenum ifmt, GLenum fmt, GLenum type, GLenum filter )
{
	GLuint t;

	pglGenTextures( 1, &t );
	pglBindTexture( GL_TEXTURE_2D, t );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	pglTexImage2D( GL_TEXTURE_2D, 0, ifmt, agua.w, agua.h, 0, fmt, type, NULL );
	return t;
}

/*
==============
R_AguaDentro

the eye is in water: that of the world (its leaf), or a func_water (the player, as the engine knows)
==============
*/
static qboolean R_AguaDentro( void )
{
	mleaf_t *leaf = gEngfuncs.Mod_PointInLeaf( RI.rvp.vieworigin, WORLDMODEL->nodes, WORLDMODEL );

	if( leaf && IsLiquidContents( leaf->contents ))
		return true;
	return agua.nents > 0 && ENGINE_GET_PARM( PARM_WATER_LEVEL ) >= 3;
}

/*
==============
R_AguaEspejo

the mirror gl_bump.c drew this frame on the plane of this surface (N, d), on unit 3, for the shader;
none: the reflection is left to gl_post.c
==============
*/
#define AGUA_ESPEJOS	16	// at least gl_bump.c MAX_PLANES
static struct
{
	int		n;
	GLuint		tex[AGUA_ESPEJOS], dep[AGUA_ESPEJOS];
	float		zfar[AGUA_ESPEJOS], blur[AGUA_ESPEJOS], fade[AGUA_ESPEJOS], tanx, tany;
	vec4_t		plane[AGUA_ESPEJOS];
	vec3_t		eye[AGUA_ESPEJOS], vf[AGUA_ESPEJOS], vr[AGUA_ESPEJOS], vu[AGUA_ESPEJOS];
	int		actual;
} espejos;

static void R_AguaEspejo( const vec3_t N, float d )
{
	int k;

	for( k = 0; k < espejos.n; k++ )
		if( DotProduct( espejos.plane[k], N ) > 0.999f && fabs( espejos.plane[k][3] - d ) < 1.0f )
			break;
	if( k >= espejos.n )
		k = -1;
	if( k == espejos.actual )
		return;
	espejos.actual = k;

	pglUniform1fARB( agua.u_hayespejo, k >= 0 ? 1.0f : 0.0f );
	if( k < 0 )
		return;
	pglActiveTextureARB( GL_TEXTURE0_ARB + 3 );
	pglBindTexture( GL_TEXTURE_2D, espejos.tex[k] );
	pglActiveTextureARB( GL_TEXTURE0_ARB );
	pglUniform4fARB( agua.u_epn, espejos.plane[k][0], espejos.plane[k][1], espejos.plane[k][2], espejos.plane[k][3] );
	pglUniform3fARB( agua.u_epe, espejos.eye[k][0], espejos.eye[k][1], espejos.eye[k][2] );
	pglUniform3fARB( agua.u_epf, espejos.vf[k][0], espejos.vf[k][1], espejos.vf[k][2] );
	pglUniform3fARB( agua.u_epr, espejos.vr[k][0], espejos.vr[k][1], espejos.vr[k][2] );
	pglUniform3fARB( agua.u_epu, espejos.vu[k][0], espejos.vu[k][1], espejos.vu[k][2] );
	pglUniform1fARB( agua.u_efade, espejos.fade[k] );
}

/*
==============
R_AguaDibujar

the water surfaces of the world kept aside by R_DrawWorld (draw_wateralpha), with the shader
==============
*/
void R_AguaDibujar( int first, int last )
{
	int w = RI.rvp.viewport[2], h = RI.rvp.viewport[3];

	// what is under the water: the view drawn so far
	if( !agua.color || agua.w != w || agua.h != h )
	{
		if( agua.color ) pglDeleteTextures( 1, &agua.color );
		if( agua.depth ) pglDeleteTextures( 1, &agua.depth );
		agua.w = w; agua.h = h;
		if( GL_Support( GL_ARB_TEXTURE_FLOAT_EXT ))
			agua.color = R_AguaTexture( GL_RGBA16F_ARB, GL_RGBA, GL_FLOAT, GL_LINEAR );
		else agua.color = R_AguaTexture( GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR );
		agua.depth = R_AguaTexture( GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST );
	}
	pglActiveTextureARB( GL_TEXTURE0_ARB + 1 );
	pglBindTexture( GL_TEXTURE_2D, agua.color );
	pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, RI.rvp.viewport[0], RI.rvp.viewport[1], w, h );
	pglActiveTextureARB( GL_TEXTURE0_ARB + 2 );
	pglBindTexture( GL_TEXTURE_2D, agua.depth );
	pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, RI.rvp.viewport[0], RI.rvp.viewport[1], w, h );
	pglActiveTextureARB( GL_TEXTURE0_ARB );

	pglUseProgramObjectARB( agua.prog );
	pglUniform1iARB( agua.u_agua, 0 );
	pglUniform1iARB( agua.u_fondo, 1 );
	pglUniform1iARB( agua.u_fondoz, 2 );
	pglUniform2fARB( agua.u_pantalla, 1.0f / w, 1.0f / h );
	pglUniform3fARB( agua.u_eye, RI.rvp.vieworigin[0], RI.rvp.vieworigin[1], RI.rvp.vieworigin[2] );
	pglUniform1fARB( agua.u_tiempo, gp_cl->time );
	pglUniform1fARB( agua.u_znear, 4.0f );
	pglUniform1fARB( agua.u_zfar, Q_max( 256.0f, RI.farClip ));
	pglUniform1fARB( agua.u_claridad, r_agua_claridad.value );
	pglUniform1fARB( agua.u_ondas, Q_max( r_agua_ondas.value, 0.0f ));
	pglUniform1fARB( agua.u_refl, Q_max( r_agua_reflejo.value, 0.0f ));
	pglUniform1fARB( agua.u_dibujo, bound( 0.0f, r_agua_textura.value, 1.0f ));
	pglUniform1fARB( agua.u_dentro, R_AguaDentro( ) ? 1.0f : 0.0f );

	// the mirrors of this frame, and how the water reflects them
	espejos.tanx = espejos.tany = 1.0f;
	espejos.n = R_BumpMirrors( espejos.tex, espejos.dep, espejos.zfar, espejos.plane, espejos.eye, espejos.vf, espejos.vr, espejos.vu, espejos.blur, espejos.fade, &espejos.tanx, &espejos.tany );
	espejos.actual = -2;
	pglUniform1iARB( agua.u_espejo, 3 );
	pglUniform2fARB( agua.u_eptan, espejos.tanx, espejos.tany );
	pglUniform1fARB( agua.u_eseno, sin( DEG2RAD( bound( 1.0f, r_agua_reflejo_angulo.value, 90.0f ))));
	pglUniform1fARB( agua.u_eondas, Q_max( r_agua_reflejo_ondas.value, 0.0f ));

	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglDepthMask( GL_TRUE );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	Matrix4x4_LoadIdentity( RI.objectMatrix );
	R_AguaMatriz( );

	for( int i = first; i <= last; i++ )
	{
		texture_t *t = WORLDMODEL->textures[i];
		msurface_t *s;

		if( !t || !( s = t->texturechain ) || !FBitSet( s->flags, SURF_DRAWTURB ))
			continue;

		for( ; s; s = s->texturechain )
		{
			vec3_t N;

			VectorCopy( s->plane->normal, N );
			if( FBitSet( s->flags, SURF_PLANEBACK ))
				VectorNegate( N, N );
			pglUniform3fARB( agua.u_nplano, N[0], N[1], N[2] );
			R_BumpAguaPlano( s, N );
			R_AguaEspejo( N, FBitSet( s->flags, SURF_PLANEBACK ) ? -s->plane->dist : s->plane->dist );
			R_AguaPolys( s );
		}
		t->texturechain = NULL;
	}

	for( int i = 0; i < agua.nents; i++ )
	{
		cl_entity_t *e = agua.ents[i];
		model_t *m = e->model;

		RI.currententity = e;
		RI.currentmodel = m;
		R_RotateForEntity( e );
		R_AguaMatriz( );
		for( int j = 0; j < m->nummodelsurfaces; j++ )
		{
			msurface_t *s = &m->surfaces[m->firstmodelsurface + j];
			vec3_t N, r;

			if( !FBitSet( s->flags, SURF_DRAWTURB ))
				continue;
			VectorCopy( s->plane->normal, N );
			if( FBitSet( s->flags, SURF_PLANEBACK ))
				VectorNegate( N, N );
			Matrix4x4_VectorRotate( RI.objectMatrix, N, r );
			pglUniform3fARB( agua.u_nplano, r[0], r[1], r[2] );
			R_BumpAguaPlano( s, r );
			{
				// its plane where the entity is now, as gl_bump.c takes it
				vec3_t ofs;

				VectorSet( ofs, RI.objectMatrix[0][3], RI.objectMatrix[1][3], RI.objectMatrix[2][3] );
				R_AguaEspejo( r, ( FBitSet( s->flags, SURF_PLANEBACK ) ? -s->plane->dist : s->plane->dist ) + DotProduct( r, ofs ));
			}
			R_AguaPolys( s );
		}
	}
	agua.nents = 0;
	RI.currententity = CL_GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;
	R_LoadIdentity( );

	pglUseProgramObjectARB( 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB + 3 ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB + 2 ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB + 1 ); pglBindTexture( GL_TEXTURE_2D, 0 );
	pglActiveTextureARB( GL_TEXTURE0_ARB );
	glState.currentTextures[1] = glState.currentTextures[2] = glState.currentTextures[3] = -1;
	glState.currentTexturesIndex[1] = glState.currentTexturesIndex[2] = glState.currentTexturesIndex[3] = -1;
}
