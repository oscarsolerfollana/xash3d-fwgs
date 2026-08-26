/*
dbg_weapon.h - dump the mod-visible weapon prediction state on change
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef DBG_WEAPON_H
#define DBG_WEAPON_H

#define DBG_WEAPON_SV 0
#define DBG_WEAPON_CL 1

struct clientdata_s;
struct weapon_data_s;

void DbgWeapon_Init( void );
void DbgWeapon_Report( int side, const struct clientdata_s *cd, const struct weapon_data_s *wd );
void DbgWeapon_Received( const struct clientdata_s *cd, const struct weapon_data_s *wd );
void DbgWeapon_Clock( double gtime, double frametime, double svtime, double timebase );
void DbgWeapon_ForcedAngle( const char *kind, float a, float b, float c );
void DbgWeapon_Event( int side, const char *name, const float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 );

#endif // DBG_WEAPON_H
