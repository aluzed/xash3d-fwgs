/*
smoke_volume.h - the smoke grenade's cloud as a volume (story 061)
Copyright (C) 2026 aluzed

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
*/
#pragma once

void SmokeVolume_Init( void );
void SmokeVolume_Reset( void );
bool SmokeVolume_Enabled( void );
void SmokeVolume_Create( const Vector &origin, int seed, bool light );
void SmokeVolume_Blast( const Vector &origin, float radius );
void SmokeVolume_Draw( void );
