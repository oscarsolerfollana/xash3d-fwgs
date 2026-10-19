/*
variantes.h - weapon variants: a new weapon that rides on an existing one
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

#ifndef VARIANTES_H
#define VARIANTES_H

#define MAX_VARIANTES		16
#define MAX_VARIANTE_SONIDOS	4

/*
A variant is a weapon the game dll does not know about. It is chosen with a
class menu option the game never sees (the engine passes the base weapon's
option on) and looks and sounds like itself: its own view, player and world
models and its own fire sound. Everything else (damage, clip, rate of fire)
is the base weapon's. Read from variantes.txt in the game directory.
*/
typedef struct
{
	char	token[32];	// as in the class menu files
	int	menu;		// VGUI menu the base weapon is bought in
	int	opcion;		// option the client sends for the variant
	int	base;		// option of the base weapon
	char	v_base[128], v_nuevo[64];	// the base ones can be comma separated lists
	char	p_base[128], p_nuevo[64];
	char	w_base[128], w_nuevo[64];
	char	sonidos_base[MAX_VARIANTE_SONIDOS][64];	// prefixes of the base fire sounds
	int	num_sonidos;
	char	sonido[64];	// fire sound of the variant
	char	cargador_arma[32];	// optional: weapon entity of the base, for the settings below
	int	cargador_base;	// magazine size of the base weapon (0: unchanged)
	int	cargador_nuevo;	// magazine size of the variant
	float	cadencia;	// rate of fire, times the base's (1: unchanged)
	float	recarga;	// reload time, times the base's (1: unchanged)
	int	repuesto;	// spare magazines more (or fewer, < 0) than the base gets
	float	lanzamiento;	// speed of what is thrown (grenades), times the base's
	float	dano;		// damage of its bullets, times the base's
	float	precision;	// how far off the view its bullets go, times the base's
	float	retroceso;	// how far each shot kicks the view, times the base's
	float	lateral;	// and to the sides, times that
} variante_t;

int Variantes_Cargar( variante_t *out, int max );
qboolean Variantes_EnLista( const char *lista, const char *nombre );

#endif // VARIANTES_H
