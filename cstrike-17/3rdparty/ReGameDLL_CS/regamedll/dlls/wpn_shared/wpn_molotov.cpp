#include "precompiled.h"

/*
The molotov (terrorists) and incendiary grenade (counter-terrorists): thrown
like the other grenades, the bottle is made by Molotov_Throw (molotov.cpp).
The view keeps the smoke grenade's arms, the client puts the bottle or the
canister in them; Molotov_Model picks the other models by team.
*/

LINK_ENTITY_TO_CLASS(weapon_molotov, CMolotov, CCSPlayerWeapon)

void CMolotov::Spawn()
{
	Precache();

	m_iId = WEAPON_MOLOTOV;
	SET_MODEL(edict(), Molotov_Model('w', TERRORIST));

	m_iDefaultAmmo = MOLOTOV_DEFAULT_GIVE;
	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

	// Get ready to fall down
	FallInit();

	// extend
	CBasePlayerWeapon::Spawn();
}

void CMolotov::Precache()
{
	PRECACHE_MODEL(MOLOTOV_MODEL_VIEW);
	PRECACHE_MODEL(MOLOTOV_MODEL_PLAYER);
	PRECACHE_MODEL(MOLOTOV_MODEL_WORLD);

	PRECACHE_SOUND("weapons/pinpull.wav");

	Molotov_Precache();
}

int CMolotov::GetItemInfo(ItemInfo *p)
{
	auto info = GetWeaponInfo(WEAPON_MOLOTOV);

	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "Molotov";

	p->iMaxAmmo1 = info ? info->maxRounds : MAX_AMMO_MOLOTOV;
	p->iMaxClip = WEAPON_NOCLIP;

	p->pszAmmo2 = nullptr;
	p->iMaxAmmo2 = -1;
	p->iSlot = 3;
	p->iPosition = 4;
	p->iId = m_iId = WEAPON_MOLOTOV;
	p->iWeight = MOLOTOV_WEIGHT;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;

	return 1;
}

BOOL CMolotov::Deploy()
{
	m_flReleaseThrow = -1;
	m_fMaxSpeed = MOLOTOV_MAX_SPEED;

	return DefaultDeploy(MOLOTOV_MODEL_VIEW, (char *)Molotov_Model('p', m_pPlayer->m_iTeam), SMOKEGRENADE_DRAW, "grenade", UseDecrement() != FALSE);
}

void CMolotov::Holster(int skiplocal)
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if (!m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
	{
		// no more molotovs
		DestroyItem();
	}

	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
}

void CMolotov::PrimaryAttack()
{
	if (!m_flStartThrow && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0)
	{
		m_flReleaseThrow = 0;
		m_flStartThrow = gpGlobals->time;

		SendWeaponAnim(SMOKEGRENADE_PINPULL, UseDecrement() != FALSE);
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
	}
}

// the throw of the other grenades
void CMolotov::Throw()
{
	m_pPlayer->Radio("%!MRAD_FIREINHOLE", "#Fire_in_the_hole");

	Vector angThrow = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;

	if (angThrow.x < 0)
		angThrow.x = -10 + angThrow.x * ((90 - 10) / 90.0);
	else
		angThrow.x = -10 + angThrow.x * ((90 + 10) / 90.0);

	real_t flVel = (90.0f - angThrow.x) * 6.0f;

	if (flVel > 750.0f)
		flVel = 750.0f;

	UTIL_MakeVectors(angThrow);

	Vector vecSrc = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs + gpGlobals->v_forward * 16.0f;
	Vector vecThrow = gpGlobals->v_forward * flVel * Molotov_ThrowScale() + m_pPlayer->pev->velocity;

	Molotov_Throw(m_pPlayer, vecSrc, vecThrow);
}

void CMolotov::WeaponIdle()
{
	if (m_flReleaseThrow == 0)
		m_flReleaseThrow = gpGlobals->time;

	if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	if (m_flStartThrow)
	{
		Throw();

		SendWeaponAnim(SMOKEGRENADE_THROW, UseDecrement() != FALSE);

		// player "shoot" animation
		m_pPlayer->SetAnimation(PLAYER_ATTACK1);

		m_flStartThrow = 0;
		m_flNextPrimaryAttack = GetNextAttackDelay(0.5);
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.75f;

		if (--m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
		{
			// just threw the last one: let the throw animation finish,
			// weapon idle then retires the weapon
			m_flTimeWeaponIdle = m_flNextSecondaryAttack = m_flNextPrimaryAttack = GetNextAttackDelay(0.5);
		}
	}
	else if (m_flReleaseThrow > 0)
	{
		// we've finished the throw, restart.
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

BOOL CMolotov::CanDeploy()
{
	return m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0;
}
