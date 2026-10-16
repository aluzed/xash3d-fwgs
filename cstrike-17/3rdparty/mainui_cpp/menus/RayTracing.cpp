/*
RayTracing.cpp -- the Vulkan renderer's quality and its ray traced effects,
a page of their own under Video (they were at the foot of Video options)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "Framework.h"
#include "PicButton.h"
#include "Slider.h"
#include "CheckBox.h"
#include "Action.h"
#include "SpinControl.h"
#include "StringArrayModel.h"

#define ART_BANNER "gfx/shell/head_vidoptions"

class CMenuRayTracing : public CMenuFramework
{
private:
	void _Init() override;
	void Reload() override;

public:
	CMenuRayTracing() : CMenuFramework( "CMenuRayTracing" ) { }
	void SaveAndPopMenu() override;

	CMenuPicButton   done;
	CMenuAction      status;
	CMenuSpinControl quality; // r_vk_preset
	CMenuCheckBox    rtShadows, rtOcclusion, rtReflections, rtLighting;
	CMenuSlider      dlight;  // r_vk_rt_dlight
};

void CMenuRayTracing::SaveAndPopMenu()
{
	rtShadows.WriteCvar();
	rtOcclusion.WriteCvar();
	rtReflections.WriteCvar();
	rtLighting.WriteCvar();
	dlight.WriteCvar();
	CMenuFramework::SaveAndPopMenu();
}

void CMenuRayTracing::_Init( void )
{
	banner.SetPicture( ART_BANNER );

	int height = 230;

	status.iFlags = QMF_INACTIVE | QMF_DROPSHADOW;
	status.colorBase = uiColorHelp;
	status.SetCharSize( QM_SMALLFONT );
	status.SetCoord( 72, height );
	height += 50;

	// the values of r_vk_preset, in the order of the names; another value
	// (a setting changed by hand) shows as the last one
	static const char *qualityValues[] = { "classic", "low", "medium", "high", "ultra" };
	static const char *qualityNames[] =
	{
		L( "Classic" ),
		L( "Low (HDR)" ),
		L( "Medium (HDR)" ),
		L( "High (ray tracing)" ),
		L( "Ultra (ray tracing)" ),
		L( "Custom" ),
	};
	static CStringArrayModel qualityModel( qualityNames, V_ARRAYSIZE( qualityNames ));

	quality.szName = L( "Rendering quality" );
	quality.Setup( &qualityModel );
	quality.SetRect( 72, height + 25, 300, 32 );
	quality.SetCharSize( QM_SMALLFONT );
	quality.onChanged = CMenuEditable::WriteCvarCb; // applies at once, no restart
	quality.onCvarGet.pExtra = (void *)qualityValues;
	SET_EVENT_MULTI( quality.onCvarGet,
	{
		CMenuSpinControl *spin = (CMenuSpinControl *)pSelf;
		const char **values = (const char **)pExtra;
		const char *cvar = EngFuncs::GetCvarString( spin->CvarName( ));
		int i;

		for( i = 0; i < 5; i++ )
		{
			if( !stricmp( cvar, values[i] ))
				break;
		}

		spin->SetCurrentValue( (float)i );
	});
	quality.onCvarWrite.pExtra = (void *)qualityValues;
	SET_EVENT_MULTI( quality.onCvarWrite,
	{
		CMenuSpinControl *spin = (CMenuSpinControl *)pSelf;
		const char **values = (const char **)pExtra;
		const int i = (int)spin->GetCurrentValue();

		// "Custom" is what the settings are: nothing to set
		if( i < 5 )
			EngFuncs::CvarSetString( spin->CvarName(), values[i] );
	});
	height += 90;

	// each ray traced effect, on top of the preset: the preset becomes custom
	CMenuCheckBox *boxes[] = { &rtShadows, &rtOcclusion, &rtReflections, &rtLighting };
	static const char *names[] = { L( "Shadows" ), L( "Ambient occlusion" ), L( "Reflections" ), L( "Lighting (global illumination)" ) };
	for( int i = 0; i < 4; i++ )
	{
		boxes[i]->szName = names[i];
		boxes[i]->SetCoord( 72, height );
		height += 50;
	}

	// how bright the explosions and flashes light the place
	dlight.szName = L( "Explosions' light" );
	dlight.SetCoord( 72, height + 20 );
	dlight.Setup( 1.0f, 3.0f, 0.25f );
	dlight.onChanged = CMenuEditable::WriteCvarCb;
	height += 80;

	done.szName = L( "GameUI_OK" );
	done.SetCoord( 72, height );
	done.SetPicture( PC_DONE );
	done.onReleased = VoidCb( &CMenuRayTracing::SaveAndPopMenu );

	AddItem( banner );
	AddItem( status );
	AddItem( quality );
	AddItem( rtShadows );
	AddItem( rtOcclusion );
	AddItem( rtReflections );
	AddItem( rtLighting );
	AddItem( dlight );
	AddItem( done );
}

void CMenuRayTracing::Reload()
{
	bool vk_active = !stricmp( EngFuncs::GetCvarString( "r_refdll_loaded" ), "vk" );
	// the ray traced effects need a GPU (and a driver: 64-bit on AMD) that traces rays
	bool rt_active = vk_active && EngFuncs::GetCvarFloat( "r_vk_rt_supported" ) != 0.0f;
	CMenuCheckBox *boxes[] = { &rtShadows, &rtOcclusion, &rtReflections, &rtLighting };

	if( !vk_active )
		status.szName = L( "Needs the Vulkan renderer: Video modes, Renderer, Vulkan" );
	else if( !rt_active )
		status.szName = L( "No ray tracing on this GPU or driver" );
	else status.szName = L( "Ray tracing available" );

	quality.SetGrayed( !vk_active );
	quality.SetInactive( !vk_active );
	if( vk_active )
		quality.LinkCvar( "r_vk_preset", CMenuEditable::CVAR_STRING );
	else quality.SetCurrentValue( 0.0f ); // Classic, as the other renderers draw

	for( int i = 0; i < 4; i++ )
	{
		boxes[i]->SetGrayed( !rt_active );
		boxes[i]->SetInactive( !rt_active );
	}
	dlight.SetGrayed( !rt_active );
	dlight.SetInactive( !rt_active );

	if( vk_active )
	{
		rtShadows.LinkCvar( "r_vk_rt_shadows" );
		rtOcclusion.LinkCvar( "r_vk_rt_ao" );
		rtReflections.LinkCvar( "r_vk_rt_reflections" );
		rtLighting.LinkCvar( "r_vk_rt_gi" );
		dlight.LinkCvar( "r_vk_rt_dlight" );
	}

	CMenuFramework::Reload();
}

ADD_MENU( menu_raytracing, CMenuRayTracing, UI_RayTracing_Menu );
