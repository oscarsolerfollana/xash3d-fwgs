/*
gl_humo.c - soft smoke: the puffs fade where they cross the walls (custom)
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
The puffs of the volumetric smoke (engine/client/cl_humo.c, sprites/humo_*.spr)
are big flat sprites: where one crosses the floor or a wall it is cut with a
straight line, and that is what gives them away as flat. Drawn with this shader
they fade as they get near what is behind them (soft particles): the depth of
the view drawn so far is copied once a frame, the first time a puff is drawn,
and each pixel of the puff is as transparent as it is close to it. They also
fade when the eye goes into them, instead of filling the screen at once.
Only in the main view; the mirrors and the cube draw them as always.
*/

CVAR_DEFINE_AUTO( r_humo_suave, "48", FCVAR_ARCHIVE, "custom: units over which the smoke fades into the walls and the floor (0 cut as always)" );

static const char *vs_humo =
"varying float vz;\n"
"void main( void )\n"
"{\n"
"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"	gl_FrontColor = gl_Color;\n"
"	vz = -( gl_ModelViewMatrix * gl_Vertex ).z;\n"
"	gl_Position = ftransform();\n"
"}\n";

static const char *fs_humo =
"uniform sampler2D tex, prof;\n"
"uniform vec4 pantalla;\n"	// 1 / size, corner of the viewport
"uniform float znear, zfar, suave;\n"
"varying float vz;\n"
"float lineal( float d ) { float z = d * 2.0 - 1.0; return 2.0 * znear * zfar / ( zfar + znear - z * ( zfar - znear )); }\n"
"void main( void )\n"
"{\n"
"	vec4 c = texture2D( tex, gl_TexCoord[0].xy ) * gl_Color;\n"
"	float zs = lineal( texture2D( prof, ( gl_FragCoord.xy - pantalla.zw ) * pantalla.xy ).r );\n"
"	float k = clamp(( zs - vz ) / suave, 0.0, 1.0 );\n"
"	k *= clamp(( vz - 8.0 ) / 48.0, 0.0, 1.0 );\n"	// the eye inside
"	gl_FragColor = vec4( c.rgb, c.a * k * k * ( 3.0 - 2.0 * k ));\n"
"}\n";

static struct
{
	qboolean	tried, ok;
	GLhandleARB	prog;
	GLint		u_tex, u_prof, u_pantalla, u_znear, u_zfar, u_suave;
	GLuint		depth;
	int		w, h;
	int		copiado;	// tr.realframecount of the copy
	qboolean	activo;
} humo;

void R_InitHumo( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_humo_suave );
	memset( &humo, 0, sizeof( humo ));
	humo.copiado = -1;
}

static GLhandleARB R_HumoShader( GLenum type, const char *src )
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
		gEngfuncs.Con_Printf( S_ERROR "r_humo_suave: shader: %s\n", log );
		return 0;
	}
	return s;
}

static qboolean R_HumoProgram( void )
{
	GLhandleARB v, f;
	GLint ok = 0;
	char log[2048];

	if( humo.tried )
		return humo.ok;
	humo.tried = true;

	if( !GL_Support( GL_SHADER_GLSL100_EXT ) || !GL_Support( GL_SHADER_OBJECTS_EXT ) || glConfig.max_texture_units < 2 )
		return false;
	if( !( v = R_HumoShader( GL_VERTEX_SHADER_ARB, vs_humo )) || !( f = R_HumoShader( GL_FRAGMENT_SHADER_ARB, fs_humo )))
		return false;
	humo.prog = pglCreateProgramObjectARB();
	pglAttachObjectARB( humo.prog, v );
	pglAttachObjectARB( humo.prog, f );
	pglLinkProgramARB( humo.prog );
	pglGetObjectParameterivARB( humo.prog, GL_OBJECT_LINK_STATUS_ARB, &ok );
	if( !ok )
	{
		pglGetInfoLogARB( humo.prog, sizeof( log ), NULL, log );
		gEngfuncs.Con_Printf( S_ERROR "r_humo_suave: program: %s\n", log );
		return false;
	}
	humo.u_tex = pglGetUniformLocationARB( humo.prog, "tex" );
	humo.u_prof = pglGetUniformLocationARB( humo.prog, "prof" );
	humo.u_pantalla = pglGetUniformLocationARB( humo.prog, "pantalla" );
	humo.u_znear = pglGetUniformLocationARB( humo.prog, "znear" );
	humo.u_zfar = pglGetUniformLocationARB( humo.prog, "zfar" );
	humo.u_suave = pglGetUniformLocationARB( humo.prog, "suave" );
	humo.ok = true;
	return true;
}

/*
==============
R_HumoEmpezar

from R_DrawSpriteModel, before the quad: true when this sprite is a puff and the shader is on
==============
*/
qboolean R_HumoEmpezar( cl_entity_t *e )
{
	int w = RI.rvp.viewport[2], h = RI.rvp.viewport[3];

	humo.activo = false;

	if( r_humo_suave.value <= 0.0f || !e->model || e->curstate.rendermode != kRenderTransAlpha )
		return false;
	if( Q_strncmp( e->model->name, "sprites/humo_", 13 ))
		return false;
	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return false;
	if( R_BumpDrawingPlanar( ) || R_BumpDrawingDynamic( ) || w <= 0 || h <= 0 )
		return false;
	if( !R_HumoProgram( ))
		return false;

	if( !humo.depth || humo.w != w || humo.h != h )
	{
		if( humo.depth ) pglDeleteTextures( 1, &humo.depth );
		humo.w = w; humo.h = h;
		pglGenTextures( 1, &humo.depth );
		pglBindTexture( GL_TEXTURE_2D, humo.depth );
		pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		pglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL );
		humo.copiado = -1;
	}

	GL_SelectTexture( XASH_TEXTURE1 );
	pglBindTexture( GL_TEXTURE_2D, humo.depth );
	// the puffs don't write depth (kRenderTransAlpha): one copy serves them all
	if( humo.copiado != tr.realframecount )
	{
		pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, RI.rvp.viewport[0], RI.rvp.viewport[1], w, h );
		humo.copiado = tr.realframecount;
	}
	glState.currentTextures[XASH_TEXTURE1] = -1;
	glState.currentTexturesIndex[XASH_TEXTURE1] = -1;
	GL_SelectTexture( XASH_TEXTURE0 );

	pglUseProgramObjectARB( humo.prog );
	pglUniform1iARB( humo.u_tex, 0 );
	pglUniform1iARB( humo.u_prof, 1 );
	pglUniform4fARB( humo.u_pantalla, 1.0f / w, 1.0f / h, RI.rvp.viewport[0], RI.rvp.viewport[1] );
	pglUniform1fARB( humo.u_znear, 4.0f );
	pglUniform1fARB( humo.u_zfar, Q_max( 256.0f, RI.farClip ));
	pglUniform1fARB( humo.u_suave, Q_max( r_humo_suave.value, 1.0f ));

	humo.activo = true;
	return true;
}

void R_HumoTerminar( void )
{
	if( !humo.activo )
		return;

	pglUseProgramObjectARB( 0 );
	humo.activo = false;
}
