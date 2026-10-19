/*
ads_bala.c - the bullets of who aims down the sights
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

/*
The game spreads the bullets itself, off the view by a random angle: aiming
down the sights (cl_ads.c) that angle is made smaller, the trace of the bullet
is turned towards the view. A weapon variant (variantes.h) can make it smaller
or bigger too. The server does it to the shot (sv_game.c) and the
client to the impacts it draws (cl_ads.c), the same way.

With the realistic precision (precision_realista) the spread of the game is
taken away (k 0) and the bullet goes off the view only by what the weapon
does by itself, moa minutes of angle (armas_reales.c), at random.

A bullet is a long trace from the eye not far off the view. The trace through a
wall after it (Firearms) goes from past the hole to the same end: it gets the
new one
*/

#define ADS_BALA_MIN	256.0f	// shorter traces aren't bullets
#define ADS_BALA_CONO	15.0f	// degrees off the view, at most

typedef struct
{
	const void	*quien;
	double		hora;
	vec3_t		fin_juego;	// the end the game asked for
	vec3_t		fin;	// the one given instead
} ads_bala_t;

static ads_bala_t	ultima[2];	// the server's and the client's

/*
==============
COM_AdsBala

the end the trace from v1 to v2 has to have. quien and hora tell one shooter
and frame from another, lado is 0 on the server and 1 on the client. moa,
when above 0, puts the bullet off by up to that much, at random, after k
==============
*/
const float *COM_AdsBala( int lado, const void *quien, double hora, const vec3_t ojo, const vec3_t angulos, float k, float moa, const float *v1, const float *v2, vec3_t fin )
{
	ads_bala_t	*u = &ultima[lado & 1];
	vec3_t		dir, vista, lado_v, arriba;
	float		largo;

	if(( k == 1.0f && moa <= 0.0f ) || k < 0.0f )
		return v2;

	// through a wall: towards the new end
	if( u->quien == quien && u->hora == hora && VectorCompare( v2, u->fin_juego ))
	{
		VectorCopy( u->fin, fin );
		return fin;
	}

	if( VectorDistance( v1, ojo ) > 4.0f )
		return v2;

	VectorSubtract( v2, v1, dir );
	largo = VectorNormalizeLength( dir );
	if( largo < ADS_BALA_MIN )
		return v2;

	AngleVectors( angulos, vista, lado_v, arriba );
	if( DotProduct( dir, vista ) < cos( DEG2RAD( ADS_BALA_CONO )))
		return v2;

	// what is off the view, times k
	VectorSubtract( dir, vista, dir );
	VectorMA( vista, k, dir, dir );
	VectorNormalize( dir );

	// what the weapon does by itself: anywhere in a circle of moa across
	if( moa > 0.0f )
	{
		float	r = tan( DEG2RAD( moa / 120.0f )) * sqrt( COM_RandomFloat( 0.0f, 1.0f ));
		float	a = COM_RandomFloat( 0.0f, M_PI2_F );

		VectorMA( dir, r * cos( a ), lado_v, dir );
		VectorMA( dir, r * sin( a ), arriba, dir );
		VectorNormalize( dir );
	}
	VectorMA( v1, largo, dir, fin );

	u->quien = quien;
	u->hora = hora;
	VectorCopy( v2, u->fin_juego );
	VectorCopy( fin, u->fin );
	return fin;
}
