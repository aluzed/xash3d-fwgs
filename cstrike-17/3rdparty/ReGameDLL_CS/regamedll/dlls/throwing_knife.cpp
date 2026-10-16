#include "precompiled.h"

/*
The thrown knife (stories 062 and 063 of the xash3d-fwgs fork)

With the knife in hand, the use key held and the primary attack throws it
straight: fast, flat for a moment, then falling, a short range. With the
secondary attack it is lobbed, on the very flight of an HE grenade thrown
the same way: harmless going up, deadly coming down. The thrower is left
without a knife; the knife lies where it fell and anyone without one picks it up by walking over it.

The knife in flight (throwing_knife, the name the kill feed shows) moves
without the engine's collisions: each frame it traces its way against the
world and the players' hitboxes, so that a hit knows its hit group, and
the damage is set by hit group (and, lobbed, by the way it goes), neither
multiplied as a bullet's nor lessened by kevlar or helmet. After a hit it
drops and lies as a dropped weapon does.
*/

cvar_t knife_throw          = { "mp_knife_throw", "1", 0, 1.0f, nullptr };
cvar_t knife_throw_speed    = { "mp_knife_throw_speed", "1000", 0, 1000.0f, nullptr };
cvar_t knife_throw_gravity  = { "mp_knife_throw_gravity", "1.2", 0, 1.2f, nullptr };
cvar_t knife_throw_flat     = { "mp_knife_throw_flat", "0.25", 0, 0.25f, nullptr };  // seconds before it falls
cvar_t knife_throw_dmg_head = { "mp_knife_throw_dmg_head", "100", 0, 100.0f, nullptr };
cvar_t knife_throw_dmg_body = { "mp_knife_throw_dmg_body", "90", 0, 90.0f, nullptr };
cvar_t knife_throw_dmg_limb = { "mp_knife_throw_dmg_limb", "70", 0, 70.0f, nullptr };
cvar_t knife_lob_dmg_rise   = { "mp_knife_lob_dmg_rise", "45", 0, 45.0f, nullptr };
cvar_t knife_lob_dmg_head   = { "mp_knife_lob_dmg_head", "100", 0, 100.0f, nullptr };
cvar_t knife_lob_dmg_body   = { "mp_knife_lob_dmg_body", "100", 0, 100.0f, nullptr };
cvar_t knife_lob_dmg_limb   = { "mp_knife_lob_dmg_limb", "90", 0, 90.0f, nullptr };
cvar_t knife_throw_debug    = { "mp_knife_throw_debug", "0", 0, 0.0f, nullptr };  // the thrower's console: where it hit, where it lies

#define THROWN_KNIFE_MODEL      "models/w_knife.mdl"
#define THROWN_KNIFE_IGNORE     0.25f   // seconds the knife passes through its thrower
#define THROWN_KNIFE_SPIN       1080.0f // degrees a second, end over end
#define THROWN_KNIFE_BOUNCE     0.25f   // of its speed kept off a wall
#define LOBBED_KNIFE_GRAVITY    0.55f   // an HE grenade's

static const char *s_szHitSounds[] = { "weapons/knife_hit1.wav", "weapons/knife_hit2.wav", "weapons/knife_hit3.wav", "weapons/knife_hit4.wav" };

void ThrowingKnife_RegisterCvars()
{
	CVAR_REGISTER(&knife_throw);
	CVAR_REGISTER(&knife_throw_speed);
	CVAR_REGISTER(&knife_throw_gravity);
	CVAR_REGISTER(&knife_throw_flat);
	CVAR_REGISTER(&knife_throw_dmg_head);
	CVAR_REGISTER(&knife_throw_dmg_body);
	CVAR_REGISTER(&knife_throw_dmg_limb);
	CVAR_REGISTER(&knife_lob_dmg_rise);
	CVAR_REGISTER(&knife_lob_dmg_head);
	CVAR_REGISTER(&knife_lob_dmg_body);
	CVAR_REGISTER(&knife_lob_dmg_limb);
	CVAR_REGISTER(&knife_throw_debug);
}

void ThrowingKnife_Precache()
{
	PRECACHE_MODEL(THROWN_KNIFE_MODEL);
	PRECACHE_SOUND("items/gunpickup2.wav");
}

// E held with the knife in hand: a throw instead of a slash or a stab; the
// use wins while defusing, and a shield keeps the knife in the hand
bool ThrowingKnife_Wanted(CBasePlayer *pPlayer)
{
	return knife_throw.value != 0.0f && (pPlayer->pev->button & IN_USE)
		&& !pPlayer->m_bIsDefusing && !pPlayer->HasShield();
}

/*
=================
The knife in flight, then on the ground
=================
*/
class CThrownKnife: public CBaseEntity
{
public:
	virtual void Spawn();
	virtual int ObjectCaps() { return FCAP_DONT_SAVE; }

	void EXPORT FlyThink();
	void EXPORT LieThink();
	void EXPORT PickUpTouch(CBaseEntity *pOther);

	float Damage(int iHitgroup) const;
	void HitPlayer(CBasePlayer *pVictim, TraceResult &tr);
	void Drop(const Vector &vecVelocity);

	void Debug(const char *pszWhat, const char *pszDetail = "");

	EHANDLE m_hThrower;
	bool m_bLob;
	float m_flLaunch;
	Vector m_vecLaunch;
	bool m_bRested;
};

LINK_ENTITY_TO_CLASS(throwing_knife, CThrownKnife, CCSEntity)

void CThrownKnife::Spawn()
{
	// moved by its own traces, the engine only carries it along
	pev->movetype = MOVETYPE_NOCLIP;
	pev->solid = SOLID_NOT;

	SET_MODEL(edict(), THROWN_KNIFE_MODEL);
	UTIL_SetSize(pev, g_vecZero, g_vecZero);
}

void CThrownKnife::Debug(const char *pszWhat, const char *pszDetail)
{
	CBaseEntity *pThrower = m_hThrower;
	if (knife_throw_debug.value == 0.0f || !pThrower)
		return;

	ClientPrint(pThrower->pev, HUD_PRINTCONSOLE, UTIL_VarArgs("knife %s at %.0f %.0f %.0f, %.0f units, %.2f s%s\n",
		pszWhat, pev->origin.x, pev->origin.y, pev->origin.z, (pev->origin - m_vecLaunch).Length2D(),
		gpGlobals->time - m_flLaunch, pszDetail));
}

float CThrownKnife::Damage(int iHitgroup) const
{
	const bool bHead = iHitgroup == HITGROUP_HEAD;
	const bool bLimb = iHitgroup == HITGROUP_LEFTARM || iHitgroup == HITGROUP_RIGHTARM
		|| iHitgroup == HITGROUP_LEFTLEG || iHitgroup == HITGROUP_RIGHTLEG;

	if (!m_bLob)
		return bHead ? knife_throw_dmg_head.value : bLimb ? knife_throw_dmg_limb.value : knife_throw_dmg_body.value;

	// lobbed: it only hurts once it comes down
	if (pev->velocity.z > 0.0f)
		return knife_lob_dmg_rise.value;

	return bHead ? knife_lob_dmg_head.value : bLimb ? knife_lob_dmg_limb.value : knife_lob_dmg_body.value;
}

void CThrownKnife::HitPlayer(CBasePlayer *pVictim, TraceResult &tr)
{
	CBaseEntity *pThrower = m_hThrower;
	entvars_t *pevAttacker = pThrower ? pThrower->pev : pev;
	const float flDamage = Damage(tr.iHitgroup);
	const Vector vecDir = pev->velocity.Normalize();

	EMIT_SOUND(pVictim->edict(), CHAN_WEAPON, s_szHitSounds[RANDOM_LONG(0, ARRAYSIZE(s_szHitSounds) - 1)], VOL_NORM, ATTN_NORM);

	// the hit group as a bullet's TraceAttack would leave it: the headshot,
	// the pain, the death's animation
	pVictim->m_LastHitGroup = tr.iHitgroup;

	if (pVictim->TakeDamage(pev, pevAttacker, flDamage, DMG_SLASH | DMG_NEVERGIB))
	{
		SpawnBlood(tr.vecEndPos, pVictim->BloodColor(), flDamage);
		pVictim->TraceBleed(flDamage, vecDir, &tr, DMG_SLASH);
	}
}

// the knife falls from where it hit, and lies as a dropped weapon
void CThrownKnife::Drop(const Vector &vecVelocity)
{
	pev->movetype = MOVETYPE_TOSS;
	pev->solid = SOLID_TRIGGER;
	pev->owner = nullptr; // or its thrower could never touch it again
	pev->gravity = 1.0f;
	pev->friction = 0.7f;
	pev->velocity = vecVelocity;
	pev->avelocity = Vector(RANDOM_FLOAT(-300, 300), RANDOM_FLOAT(-200, 200), 0);
	UTIL_SetSize(pev, Vector(-8, -8, 0), Vector(8, 8, 4));
	UTIL_SetOrigin(pev, pev->origin);

	SetTouch(&CThrownKnife::PickUpTouch);
	SetThink(&CThrownKnife::LieThink);
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CThrownKnife::FlyThink()
{
	pev->nextthink = gpGlobals->time;

	if (!IsInWorld())
	{
		UTIL_Remove(this);
		return;
	}

	const float dt = gpGlobals->frametime;

	// flat for a moment, then falling: the straight throw's arc; the lob
	// falls from the start
	if (m_bLob || gpGlobals->time - m_flLaunch > knife_throw_flat.value)
	{
		const float flGravity = m_bLob ? LOBBED_KNIFE_GRAVITY : clamp(knife_throw_gravity.value, 0.0f, 5.0f);
		pev->velocity.z -= flGravity * CVAR_GET_FLOAT("sv_gravity") * dt;
	}

	// where the engine is about to carry it this frame
	const Vector vecEnd = pev->origin + pev->velocity * dt;
	CBaseEntity *pThrower = m_hThrower;
	edict_t *pIgnore = (pThrower && gpGlobals->time - m_flLaunch < THROWN_KNIFE_IGNORE) ? pThrower->edict() : edict();
	TraceResult tr;

	UTIL_TraceLine(pev->origin, vecEnd, dont_ignore_monsters, pIgnore, &tr);

	if (tr.flFraction >= 1.0f && !tr.fStartSolid)
		return;

	if (tr.fAllSolid)
	{
		Drop(g_vecZero);
		return;
	}

	CBaseEntity *pHit = CBaseEntity::Instance(tr.pHit);
	const Vector vecDir = pev->velocity.Normalize();
	UTIL_SetOrigin(pev, tr.vecEndPos - vecDir * 2.0f);

	if (pHit && pHit->IsPlayer() && pHit->IsAlive())
	{
		Debug("hit a player", UTIL_VarArgs(", hit group %d, %.0f damage", tr.iHitgroup, Damage(tr.iHitgroup)));
		HitPlayer(static_cast<CBasePlayer *>(pHit), tr);
		Drop(g_vecZero);
		return;
	}

	// a wall, a floor, a prop: the knife rings off it and falls
	Debug("hit a wall");
	EMIT_SOUND(edict(), CHAN_WEAPON, "weapons/knife_hitwall1.wav", VOL_NORM, ATTN_NORM);
	UTIL_Sparks(tr.vecEndPos);

	if (pHit && pHit->pev->takedamage != DAMAGE_NO)
		pHit->TakeDamage(pev, pThrower ? pThrower->pev : pev, Damage(HITGROUP_CHEST), DMG_SLASH);

	// off a wall it springs back a little, on a floor it slides and stops
	const float flKeep = (tr.vecPlaneNormal.z >= 0.7f) ? 0.1f : THROWN_KNIFE_BOUNCE;
	const Vector vecBounce = (pev->velocity - tr.vecPlaneNormal * (2.0f * DotProduct(pev->velocity, tr.vecPlaneNormal))) * flKeep;
	Drop(vecBounce);
}

void CThrownKnife::LieThink()
{
	pev->nextthink = gpGlobals->time + 0.1f;

	if (!IsInWorld())
	{
		UTIL_Remove(this);
		return;
	}

	// on the ground it lies flat, as the knife's world model does
	if (pev->flags & FL_ONGROUND)
	{
		if (!m_bRested)
			Debug("lies");
		m_bRested = true;

		pev->avelocity = g_vecZero;
		pev->angles.x = 0.0f;
		pev->angles.z = 0.0f;
	}
}

void CThrownKnife::PickUpTouch(CBaseEntity *pOther)
{
	if (!(pev->flags & FL_ONGROUND) || !pOther->IsPlayer() || !pOther->IsAlive())
		return;

	CBasePlayer *pPlayer = static_cast<CBasePlayer *>(pOther);

	// one knife each
	if (pPlayer->HasNamedPlayerItem("weapon_knife") || pPlayer->pev->iuser1)
		return;

	pPlayer->GiveNamedItemEx("weapon_knife");
	EMIT_SOUND(pPlayer->edict(), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);

	SetTouch(nullptr);
	UTIL_Remove(this);
}

void ThrowingKnife_Launch(CBasePlayer *pThrower, const Vector &vecSrc, const Vector &vecVelocity, bool bLob)
{
	CThrownKnife *pKnife = GetClassPtr<CCSEntity>((CThrownKnife *)nullptr);

	MAKE_STRING_CLASS("throwing_knife", pKnife->pev);
	pKnife->Spawn();

	UTIL_SetOrigin(pKnife->pev, vecSrc);
	pKnife->pev->velocity = vecVelocity;
	pKnife->pev->angles = UTIL_VecToAngles(vecVelocity);
	pKnife->pev->angles.x = -pKnife->pev->angles.x;
	pKnife->pev->avelocity = Vector(-THROWN_KNIFE_SPIN, 0, 0);
	pKnife->pev->owner = pThrower->edict();

	pKnife->m_hThrower = pThrower;
	pKnife->m_bLob = bLob;
	pKnife->m_flLaunch = gpGlobals->time;
	pKnife->m_vecLaunch = vecSrc;
	pKnife->m_bRested = false;
	pKnife->Debug("thrown", UTIL_VarArgs(", %.0f units/s", vecVelocity.Length()));

	pKnife->SetThink(&CThrownKnife::FlyThink);
	pKnife->pev->nextthink = gpGlobals->time;
}

// round restart: no knife left lying about
void ThrowingKnife_CleanUp()
{
	int count = 0;
	CBaseEntity *pKnife = nullptr;
	while ((pKnife = UTIL_FindEntityByClassname(pKnife, "throwing_knife")))
	{
		UTIL_Remove(pKnife);
		count++;
	}

	if (knife_throw_debug.value != 0.0f && count)
		UTIL_ClientPrintAll(HUD_PRINTCONSOLE, UTIL_VarArgs("knife: %d removed for the new round\n", count));
}
