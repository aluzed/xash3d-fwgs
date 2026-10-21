/*
interlude.h - a match's interludes: the teams shown by a camera (story 058)
Copyright (C) 2026 aluzed

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
*/
#pragma once

struct ref_params_s;

void Interlude_Init( void );
void Interlude_Reset( void );
bool Interlude_Active( void );
void Interlude_CalcRefdef( struct ref_params_s *pparams );
void Interlude_Project( void );
void Interlude_DrawNames( void );
