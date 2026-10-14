/*
gl_bloom.c - light bloom over the 3D view (custom)
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
Fixed function, so it works on any card the renderer runs on. After the
world, the entities and the view model are drawn (and before the HUD):
  1. the view is copied to a texture and drawn shrunk by halves in a corner
     of the frame (r_bloom_size pixels)
  2. bright pass: the small copy is multiplied by itself r_bloom_darken
     times (with decimals, the last time only partly), so only the bright
     parts keep some light
  3. blur: drawn shifted a pixel in x and then in y, added together, a few times
  4. the full view is put back and the blurred light is added over it,
     scaled by r_bloom_alpha
*/

CVAR_DEFINE_AUTO( r_bloom, "1", FCVAR_ARCHIVE, "custom: light bloom over bright areas" );
CVAR_DEFINE_AUTO( r_bloom_alpha, "0.8", FCVAR_ARCHIVE, "custom: bloom strength" );
CVAR_DEFINE_AUTO( r_bloom_darken, "2", FCVAR_ARCHIVE, "custom: bloom bright pass (how bright a spot has to be to glow)" );
CVAR_DEFINE_AUTO( r_bloom_size, "256", FCVAR_ARCHIVE, "custom: bloom sample size (128, 256 or 512)" );
CVAR_DEFINE_AUTO( r_bloom_debug, "0", 0, "custom: 1 shows only the bloom glow, to check it" );

static int bloom_screen;	// copy of the whole view
static int bloom_mid;		// halves on the way down to bloom_small
static int bloom_small;		// shrunk, bright passed and blurred
static int screen_w, screen_h, small_size;

static int R_BloomPow2( int v )
{
	int p = 1;
	while( p < v ) p <<= 1;
	return p;
}

void R_InitBloom( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_bloom );
	gEngfuncs.Cvar_RegisterVariable( &r_bloom_alpha );
	gEngfuncs.Cvar_RegisterVariable( &r_bloom_darken );
	gEngfuncs.Cvar_RegisterVariable( &r_bloom_size );
	gEngfuncs.Cvar_RegisterVariable( &r_bloom_debug );
	bloom_screen = bloom_mid = bloom_small = 0;
}

static qboolean R_BloomTextures( int w, int h )
{
	int size = bound( 64, (int)r_bloom_size.value, 512 );

	size = R_BloomPow2( size );
	if( size > w || size > h )
		size = R_BloomPow2( Q_min( w, h )) >> 1;

	if( !bloom_screen || screen_w != R_BloomPow2( w ) || screen_h != R_BloomPow2( h ))
	{
		screen_w = R_BloomPow2( w );
		screen_h = R_BloomPow2( h );
		bloom_screen = GL_CreateTexture( "*bloom_screen", screen_w, screen_h, NULL, TF_IMAGE );
		bloom_mid = GL_CreateTexture( "*bloom_mid", screen_w, screen_h, NULL, TF_IMAGE );
	}
	if( !bloom_small || small_size != size )
	{
		small_size = size;
		bloom_small = GL_CreateTexture( "*bloom_small", small_size, small_size, NULL, TF_IMAGE );
	}
	return bloom_screen && bloom_mid && bloom_small;
}

// quad in 2D mode (y down) with a texture copied from the framebuffer (y up)
static void R_BloomQuad( float x, float y, float w, float h, float s, float t )
{
	pglBegin( GL_QUADS );
	pglTexCoord2f( 0, t ); pglVertex2f( x, y );
	pglTexCoord2f( s, t ); pglVertex2f( x + w, y );
	pglTexCoord2f( s, 0 ); pglVertex2f( x + w, y + h );
	pglTexCoord2f( 0, 0 ); pglVertex2f( x, y + h );
	pglEnd();
}

void R_Bloom( void )
{
	int	w = gpGlobals->width, h = gpGlobals->height;
	int	i, n, size;
	float	s, t, a;

	if( !r_bloom.value || r_bloom_alpha.value <= 0.0f || tr.rotation != REF_ROTATE_NONE )
		return;

	// only the main view, covering the whole window
	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return;
	if( RI.rvp.viewport[0] != 0 || RI.rvp.viewport[1] != 0 || RI.rvp.viewport[2] != w || RI.rvp.viewport[3] != h )
		return;
	if( !R_BloomTextures( w, h ))
		return;

	size = small_size;
	s = (float)w / screen_w;
	t = (float)h / screen_h;

	R_Set2DMode( true );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_BLEND );
	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	// 1. the whole view, and shrunk into the bottom left corner by halves (shrinking it at once
	// skipped most pixels and bright edges came out in stripes)
	GL_Bind( XASH_TEXTURE0, bloom_screen );
	pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h );
	pglColor4f( 1, 1, 1, 1 );
	{
		int cw = w, ch = h;
		while( cw / 2 >= size && ch / 2 >= size )
		{
			int nw = cw / 2, nh = ch / 2;
			R_BloomQuad( 0, h - nh, nw, nh, (float)cw / screen_w, (float)ch / screen_h );
			GL_Bind( XASH_TEXTURE0, bloom_mid );
			pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, nw, nh );
			cw = nw; ch = nh;
		}
		R_BloomQuad( 0, h - size, size, size, (float)cw / screen_w, (float)ch / screen_h );
	}

	// 2. bright pass: multiply by itself
	GL_Bind( XASH_TEXTURE0, bloom_small );
	pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, size, size );
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_DST_COLOR, GL_ZERO );
	{
		// r_bloom_darken con decimales: la parte entera son pasadas enteras; la decimal f mezcla
		// la ultima, (1 - f) * x + f * x * x, para graduarlo sin saltos (Oscar: entre 0 y 1)
		float	darken = bound( 0.0f, r_bloom_darken.value, 6.0f );
		float	f;

		n = (int)darken;
		f = darken - n;
		for( i = 0; i < n; i++ )
		{
			R_BloomQuad( 0, h - size, size, size, 1, 1 );
			pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, size, size );
		}
		if( f > 0.01f )
		{
			R_BloomQuad( 0, h - size, size, size, 1, 1 );		// x * x en la esquina
			pglBlendFunc( GL_ZERO, GL_SRC_ALPHA );			// por f
			GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
			pglColor4f( 1, 1, 1, f );
			R_BloomQuad( 0, h - size, size, size, 1, 1 );
			pglBlendFunc( GL_ONE, GL_ONE );			// mas x * (1 - f)
			GL_Bind( XASH_TEXTURE0, bloom_small );
			pglColor4f( 1 - f, 1 - f, 1 - f, 1 );
			R_BloomQuad( 0, h - size, size, size, 1, 1 );
			pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, size, size );
			pglColor4f( 1, 1, 1, 1 );
		}
	}

	// 3. blur: [1 4 6 4 1]/16 at 1 pixel, in x and then in y, three times (with 7 taps 2 pixels
	// apart the glow came out in stripes); repeating a small blur gives a smooth gaussian one
	pglBlendFunc( GL_ONE, GL_ONE );
	for( int pass = 0; pass < 6; pass++ )
	{
		static const float peso[5] = { 1 / 16.0f, 4 / 16.0f, 6 / 16.0f, 4 / 16.0f, 1 / 16.0f };

		pglDisable( GL_BLEND );
		pglColor4f( 0, 0, 0, 1 );
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
		R_BloomQuad( 0, h - size, size, size, 1, 1 );	// clear the corner
		GL_Bind( XASH_TEXTURE0, bloom_small );
		pglEnable( GL_BLEND );
		for( i = -2; i <= 2; i++ )
		{
			a = peso[i + 2];
			pglColor4f( a, a, a, 1 );
			if( pass & 1 ) R_BloomQuad( 0, h - size + i, size, size, 1, 1 );
			else R_BloomQuad( i, h - size, size, size, 1, 1 );
		}
		pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, size, size );
	}

	// 4. the view back, and the glow over it (r_bloom_debug: black, only the glow)
	pglDisable( GL_BLEND );
	if( r_bloom_debug.value )
	{
		pglColor4f( 0, 0, 0, 1 );
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	}
	else
	{
		pglColor4f( 1, 1, 1, 1 );
		GL_Bind( XASH_TEXTURE0, bloom_screen );
	}
	R_BloomQuad( 0, 0, w, h, s, t );

	pglEnable( GL_BLEND );
	pglBlendFunc( GL_ONE, GL_ONE );
	a = bound( 0.0f, r_bloom_alpha.value, 2.0f );
	pglColor4f( a, a, a, 1 );
	GL_Bind( XASH_TEXTURE0, bloom_small );
	R_BloomQuad( 0, 0, w, h, 1, 1 );

	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglDisable( GL_BLEND );
	pglColor4f( 1, 1, 1, 1 );
	R_Set2DMode( false );
}
