/*
variantes.c - weapon variants table (see variantes.h)
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
#include "variantes.h"

/*
==============
Variantes_Cargar

variantes.txt, one variant per line:

  token menu option base_option  v_base v_new  p_base p_new  w_base w_new  base_fire_sounds new_fire_sound  [changes]

The base models can be comma separated lists (a weapon with a silencer has
two player models), base_fire_sounds is a comma separated list of sound name
prefixes and "-" stands for none. The last field is optional: what the
variant changes in the base weapon, as comma separated key=value pairs:
  arma=weapon_uzi	weapon entity of the base (needed by the others)
  cargador=25:64	magazine size of the base and of the variant
  cadencia=2	rate of fire, times the base's
  recarga=1.5	reload time, times the base's
  repuesto=-1	spare magazines more or fewer than the base gets
  lanzamiento=1.3	speed of a thrown grenade, times the base's
  dano=1.2	damage of its bullets, times the base's
  precision=0.7	how far off the view its bullets go, times the base's
  retroceso=1.3	how far each shot kicks the view (up and to the sides),
		times the base's
  lateral=2	the kick to the sides, times that again
(weapon_uzi:25:30 is the older form of arma and cargador.) Lines starting
with // are comments.
==============
*/
int Variantes_Cargar( variante_t *out, int max )
{
	char	*afile, *linea, *fin;
	int	n = 0;

	afile = (char *)FS_LoadFile( "variantes.txt", NULL, true );
	if( !afile )
		return 0;

	// by hand: COM_ParseFile would split the comma separated lists
	for( linea = afile; linea && *linea && n < max; linea = fin ? fin + 1 : NULL )
	{
		variante_t	*v = &out[n];
		char		*campos[13];
		char		*s, *coma;
		int		i = 0;

		fin = Q_strchr( linea, '\n' );
		if( fin )
			*fin = 0;

		for( s = linea; *s && i < 13; )
		{
			while( *s == ' ' || *s == '\t' || *s == '\r' )
				s++;
			if( !*s || ( s[0] == '/' && s[1] == '/' ))
				break;
			campos[i++] = s;
			while( *s && *s != ' ' && *s != '\t' && *s != '\r' )
				s++;
			if( *s )
				*s++ = 0;
		}

		if( i < 12 )
			continue;

		memset( v, 0, sizeof( *v ));
		Q_strncpy( v->token, campos[0], sizeof( v->token ));
		v->menu = Q_atoi( campos[1] );
		v->opcion = Q_atoi( campos[2] );
		v->base = Q_atoi( campos[3] );
		Q_strncpy( v->v_base, campos[4], sizeof( v->v_base ));
		Q_strncpy( v->v_nuevo, campos[5], sizeof( v->v_nuevo ));
		Q_strncpy( v->p_base, campos[6], sizeof( v->p_base ));
		Q_strncpy( v->p_nuevo, campos[7], sizeof( v->p_nuevo ));
		Q_strncpy( v->w_base, campos[8], sizeof( v->w_base ));
		Q_strncpy( v->w_nuevo, campos[9], sizeof( v->w_nuevo ));

		for( s = campos[10]; s && *s && v->num_sonidos < MAX_VARIANTE_SONIDOS; s = coma ? coma + 1 : NULL )
		{
			coma = Q_strchr( s, ',' );
			if( coma )
				*coma = 0;
			if( *s && Q_strcmp( s, "-" ))
				Q_strncpy( v->sonidos_base[v->num_sonidos++], s, sizeof( v->sonidos_base[0] ));
		}

		if( Q_strcmp( campos[11], "-" ))
			Q_strncpy( v->sonido, campos[11], sizeof( v->sonido ));

		v->cadencia = v->recarga = v->lanzamiento = v->dano = v->precision = v->retroceso = v->lateral = 1.0f;
		if( i > 12 && Q_strcmp( campos[12], "-" ))
		{
			if( Q_strchr( campos[12], '=' ))
			{
				for( s = campos[12]; s && *s; s = coma ? coma + 1 : NULL )
				{
					char *igual;

					coma = Q_strchr( s, ',' );
					if( coma )
						*coma = 0;
					igual = Q_strchr( s, '=' );
					if( !igual )
						continue;
					*igual++ = 0;

					if( !Q_stricmp( s, "arma" ))
						Q_strncpy( v->cargador_arma, igual, sizeof( v->cargador_arma ));
					else if( !Q_stricmp( s, "cargador" ))
					{
						char *dos = Q_strchr( igual, ':' );
						if( dos )
						{
							v->cargador_base = Q_atoi( igual );
							v->cargador_nuevo = Q_atoi( dos + 1 );
						}
					}
					else if( !Q_stricmp( s, "cadencia" ))
						v->cadencia = Q_atof( igual );
					else if( !Q_stricmp( s, "recarga" ))
						v->recarga = Q_atof( igual );
					else if( !Q_stricmp( s, "repuesto" ))
						v->repuesto = Q_atoi( igual );
					else if( !Q_stricmp( s, "lanzamiento" ))
						v->lanzamiento = Q_atof( igual );
					else if( !Q_stricmp( s, "dano" ))
						v->dano = Q_atof( igual );
					else if( !Q_stricmp( s, "precision" ))
						v->precision = Q_atof( igual );
					else if( !Q_stricmp( s, "retroceso" ))
						v->retroceso = Q_atof( igual );
					else if( !Q_stricmp( s, "lateral" ))
						v->lateral = Q_atof( igual );
				}
				if( v->cadencia <= 0.0f ) v->cadencia = 1.0f;
				if( v->recarga <= 0.0f ) v->recarga = 1.0f;
				if( v->lanzamiento <= 0.0f ) v->lanzamiento = 1.0f;
				if( v->dano <= 0.0f ) v->dano = 1.0f;
				if( v->precision < 0.0f ) v->precision = 1.0f;
				if( v->retroceso < 0.0f ) v->retroceso = 1.0f;
				if( v->lateral < 0.0f ) v->lateral = 1.0f;
			}
			else
			{
				char *base = Q_strchr( campos[12], ':' );
				char *nuevo = base ? Q_strchr( base + 1, ':' ) : NULL;

				if( nuevo )
				{
					*base = *nuevo = 0;
					Q_strncpy( v->cargador_arma, campos[12], sizeof( v->cargador_arma ));
					v->cargador_base = Q_atoi( base + 1 );
					v->cargador_nuevo = Q_atoi( nuevo + 1 );
				}
			}
		}
		n++;
	}

	Mem_Free( afile );
	return n;
}

/*
==============
Variantes_EnLista

nombre is one of the comma separated names in lista
==============
*/
qboolean Variantes_EnLista( const char *lista, const char *nombre )
{
	size_t len = Q_strlen( nombre );
	const char *s = lista;

	if( !len )
		return false;

	while( s && *s )
	{
		const char *coma = Q_strchr( s, ',' );
		size_t n = coma ? (size_t)( coma - s ) : Q_strlen( s );

		if( n == len && !Q_strnicmp( s, nombre, len ))
			return true;
		s = coma ? coma + 1 : NULL;
	}
	return false;
}
