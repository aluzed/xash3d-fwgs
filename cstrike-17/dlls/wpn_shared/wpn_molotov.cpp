/*
wpn_molotov.cpp - the molotov and incendiary grenade, client prediction

The server's weapon (ReGameDLL_CS, dlls/wpn_shared/wpn_molotov.cpp) makes the
bottle; this side only plays the view model's animations in step with it.
The smoke grenade's models until the bottle has its own.
*/

#include "stdafx.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"

#define MOLOTOV_MODEL_VIEW   "models/v_smokegrenade.mdl"
#define MOLOTOV_MODEL_PLAYER "models/p_smokegrenade.mdl"
#define MOLOTOV_MODEL_WORLD  "models/w_smokegrenade.mdl"

LINK_ENTITY_TO_CLASS(weapon_molotov, CMolotov)

void CMolotov::Spawn(void)
{
	pev->classname = MAKE_STRING("weapon_molotov");

	Precache();

	m_iId = WEAPON_MOLOTOV;
	SET_MODEL(edict(), MOLOTOV_MODEL_WORLD);

	m_iDefaultAmmo = 1;
	m_flStartThrow = 0;
	m_flReleaseThrow = -1;

	FallInit();
}

void CMolotov::Precache(void)
{
	PRECACHE_MODEL(MOLOTOV_MODEL_VIEW);
}

int CMolotov::GetItemInfo(ItemInfo *p)
{
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "Molotov";

	p->iMaxAmmo1 = 1;
	p->iMaxClip = WEAPON_NOCLIP;

	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iSlot = 3;
	p->iPosition = 4;
	p->iId = m_iId = WEAPON_MOLOTOV;
	p->iWeight = 1;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;

	return 1;
}

BOOL CMolotov::Deploy(void)
{
	m_flReleaseThrow = -1;
	m_fMaxSpeed = SMOKEGRENADE_MAX_SPEED;

	return DefaultDeploy(MOLOTOV_MODEL_VIEW, MOLOTOV_MODEL_PLAYER, SMOKEGRENADE_DRAW, "grenade", UseDecrement() != FALSE);
}

void CMolotov::Holster(int skiplocal)
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if (!m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
	{
		m_pPlayer->pev->weapons &= ~(1 << WEAPON_MOLOTOV);
		DestroyItem();
	}

	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
}

void CMolotov::PrimaryAttack(void)
{
	if (!m_flStartThrow && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0)
	{
		m_flReleaseThrow = 0;
		m_flStartThrow = gpGlobals->time;

		SendWeaponAnim(SMOKEGRENADE_PINPULL, UseDecrement() != FALSE);
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
	}
}

void CMolotov::WeaponIdle(void)
{
	if (m_flReleaseThrow == 0)
		m_flReleaseThrow = gpGlobals->time;

	if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	if (m_flStartThrow)
	{
		// the bottle itself is the server's
		SendWeaponAnim(SMOKEGRENADE_THROW, UseDecrement() != FALSE);

		m_flStartThrow = 0;
		m_flNextPrimaryAttack = GetNextAttackDelay(0.5);
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.75f;

		if (--m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
			m_flTimeWeaponIdle = m_flNextSecondaryAttack = m_flNextPrimaryAttack = GetNextAttackDelay(0.5);
	}
	else if (m_flReleaseThrow > 0)
	{
		m_flStartThrow = 0;

		if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
		{
			SendWeaponAnim(SMOKEGRENADE_DRAW, UseDecrement() != FALSE);
		}
		else
		{
			RetireWeapon();
			return;
		}

		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + RANDOM_FLOAT(10, 15);
		m_flReleaseThrow = -1;
	}
	else if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
	{
		SendWeaponAnim(SMOKEGRENADE_IDLE, UseDecrement() != FALSE);
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + RANDOM_FLOAT(10, 15);
	}
}

BOOL CMolotov::CanDeploy(void)
{
	return m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0;
}
