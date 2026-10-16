#include "precompiled.h"

/*
Molotov and incendiary grenade (story 054 of the xash3d-fwgs fork)

The bottle (molotov_projectile) flies as a flashbang does, CS2's bottle is
no heavier than its grenades, and bounces off walls and players. It bursts
when it touches a floor, or when its fuse (mp_molotov_detonate_time) runs
out: near the ground it still lays its fire, higher up it only bursts in
the air, as in Counter-Strike 2.

The fire (molotov, the name the kill feed shows) lays points of flame on
the ground around the burst, from the centre outwards, each a fire sprite;
a player standing in one burns. It lasts mp_molotov_duration seconds (the
incendiary of the counter-terrorists mp_incendiary_duration), and a smoke
grenade's cloud puts it out, or keeps it from starting.
*/

cvar_t molotov_detonate_time  = { "mp_molotov_detonate_time", "2.0", 0, 2.0f, nullptr };
cvar_t molotov_duration       = { "mp_molotov_duration", "7.0", 0, 7.0f, nullptr };
cvar_t incendiary_duration    = { "mp_incendiary_duration", "5.5", 0, 5.5f, nullptr };
cvar_t molotov_damage         = { "mp_molotov_damage", "40", 0, 40.0f, nullptr };
cvar_t molotov_radius         = { "mp_molotov_radius", "100", 0, 100.0f, nullptr };
cvar_t molotov_gravity        = { "mp_molotov_gravity", "0.55", 0, 0.55f, nullptr };
cvar_t molotov_throw_scale    = { "mp_molotov_throw_scale", "1.0", 0, 1.0f, nullptr };
cvar_t molotov_debug          = { "mp_molotov_debug", "0", 0, 0.0f, nullptr }; // draws where the fire hurts

#define MOLOTOV_SPRITE          "sprites/fire.spr"
#define MOLOTOV_BURST_SPRITE    "sprites/xfireball3.spr"
#define MOLOTOV_FLAME_CLASS     "molotov_flame"
#define MOLOTOV_MAX_FLAMES      64
#define MOLOTOV_FLAME_SPACING   40.0f  // between the flames, on a hexagonal grid
#define MOLOTOV_FLAME_SIZE      28.0f  // horizontal reach of a flame's harm: the flames' discs cover the pool without gaps
#define MOLOTOV_SPRITE_HEIGHT   96.0f  // of fire.spr, drawn from its middle
#define MOLOTOV_FLAME_HEIGHT    64.0f  // and above its ground
#define MOLOTOV_SPREAD_TIME     0.4f   // seconds for the fire to reach its size
#define MOLOTOV_DAMAGE_STEP     0.25f
#define MOLOTOV_AIR_BURST_FIRE  48.0f  // a fuse running out this close to the ground still lays the fire
#define MOLOTOV_SMOKE_RADIUS    144.0f
#define MOLOTOV_SMOKE_TIME      17.0f  // seconds a smoke cloud lasts, as the clients draw it

static const char *s_szBreakSounds[] = { "debris/bustglass1.wav", "debris/bustglass2.wav", "debris/bustglass3.wav" };
static const char *s_szBurnSounds[] = { "ambience/burning1.wav", "ambience/burning2.wav", "ambience/burning3.wav" };

void Molotov_RegisterCvars()
{
	CVAR_REGISTER(&molotov_detonate_time);
	CVAR_REGISTER(&molotov_duration);
	CVAR_REGISTER(&incendiary_duration);
	CVAR_REGISTER(&molotov_damage);
	CVAR_REGISTER(&molotov_radius);
	CVAR_REGISTER(&molotov_gravity);
	CVAR_REGISTER(&molotov_throw_scale);
	CVAR_REGISTER(&molotov_debug);
}

float Molotov_ThrowScale()
{
	return clamp(molotov_throw_scale.value, 0.1f, 2.0f);
}

// The bottle and the canister (cstrike-17 extras, made by
// scripts/make_molotov_models.py), in the hand ('p') and in the world ('w');
// the smoke grenade's on a server without them
static bool s_bMolotovModels;

const char *Molotov_Model(char kind, int team)
{
	if (!s_bMolotovModels)
		return (kind == 'p') ? MOLOTOV_MODEL_PLAYER : MOLOTOV_MODEL_WORLD;

	if (team == CT)
		return (kind == 'p') ? "models/p_incgrenade.mdl" : "models/w_incgrenade.mdl";

	return (kind == 'p') ? "models/p_molotov.mdl" : "models/w_molotov.mdl";
}

static bool Molotov_HasFile(const char *name)
{
	int length = 0;
	byte *data = LOAD_FILE_FOR_ME((char *)name, &length);
	if (!data)
		return false;

	FREE_FILE(data);
	return length > 0;
}

void Molotov_Precache()
{
	PRECACHE_MODEL(MOLOTOV_SPRITE);
	PRECACHE_MODEL(MOLOTOV_BURST_SPRITE);
	PRECACHE_MODEL(MOLOTOV_MODEL_WORLD);

	s_bMolotovModels = Molotov_HasFile("models/p_molotov.mdl") && Molotov_HasFile("models/w_molotov.mdl")
		&& Molotov_HasFile("models/p_incgrenade.mdl") && Molotov_HasFile("models/w_incgrenade.mdl");

	if (s_bMolotovModels)
	{
		PRECACHE_MODEL("models/p_molotov.mdl");
		PRECACHE_MODEL("models/w_molotov.mdl");
		PRECACHE_MODEL("models/p_incgrenade.mdl");
		PRECACHE_MODEL("models/w_incgrenade.mdl");
	}

	PRECACHE_SOUND_ARRAY(s_szBreakSounds);
	PRECACHE_SOUND_ARRAY(s_szBurnSounds);
	PRECACHE_SOUND("ambience/flameburst1.wav");
	PRECACHE_SOUND("weapons/grenade_hit1.wav");
	PRECACHE_MODEL("sprites/laserbeam.spr");
}

/*
=================
Smoke clouds: they put the fire out
=================
*/
static struct
{
	Vector origin;
	float time;
} s_smokes[16];

void Molotov_SmokeDetonated(const Vector &origin)
{
	int oldest = 0;

	for (int i = 1; i < ARRAYSIZE(s_smokes); i++)
	{
		if (s_smokes[i].time < s_smokes[oldest].time)
			oldest = i;
	}

	s_smokes[oldest].origin = origin;
	s_smokes[oldest].time = gpGlobals->time;
}

static bool Molotov_InSmoke(const Vector &origin)
{
	for (auto &smoke : s_smokes)
	{
		if (smoke.time <= 0.0f || smoke.time > gpGlobals->time || gpGlobals->time - smoke.time > MOLOTOV_SMOKE_TIME)
			continue;

		if ((smoke.origin - origin).Length() < MOLOTOV_SMOKE_RADIUS)
			return true;
	}

	return false;
}

/*
=================
The fire
=================
*/
class CMolotovFire: public CBaseEntity
{
public:
	virtual void Spawn();
	virtual int ObjectCaps() { return FCAP_DONT_SAVE; }

	void Start(CBasePlayer *pThrower, const Vector &vecGround);
	void EXPORT FireThink();

private:
	void LayFlames();
	void DrawFlameArea(const Vector &flame);
	void Extinguish();
	void Burn();

	EHANDLE m_hThrower;
	int m_iTeam;
	Vector m_vecCenter;
	float m_flStart;
	float m_flEnd;
	float m_flNextDamage;
	float m_flNextSound;
	float m_flNextLight;
	int m_iNumFlames;
	int m_iLit;
	int m_iDebugFlame;
	Vector m_vecFlames[MOLOTOV_MAX_FLAMES];
	EHANDLE m_hSprites[MOLOTOV_MAX_FLAMES];
};

// "molotov": the kill feed shows the inflictor's name
LINK_ENTITY_TO_CLASS(molotov, CMolotovFire, CCSEntity)

void CMolotovFire::Spawn()
{
	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_NOT;
	pev->effects |= EF_NODRAW;
	UTIL_SetSize(pev, g_vecZero, g_vecZero);
}

// points of flame on the ground: a hexagonal grid around the burst, out to
// mp_molotov_radius, each point reached from the centre without crossing a
// wall, on a floor; lit from the centre outwards
void CMolotovFire::LayFlames()
{
	const float radius = clamp(molotov_radius.value, 32.0f, 400.0f);
	const float rowStep = MOLOTOV_FLAME_SPACING * 0.866f;
	const float turn = RANDOM_FLOAT(0, M_PI * 2);
	const float c = Q_cos(turn), sn = Q_sin(turn);
	const Vector vecUp = m_vecCenter + Vector(0, 0, 16);
	const int rows = int(radius / rowStep);
	float dist[MOLOTOV_MAX_FLAMES];
	TraceResult tr;

	m_iNumFlames = 0;

	for (int row = -rows; row <= rows; row++)
	{
		const float y = row * rowStep;
		const float shift = (row & 1) ? MOLOTOV_FLAME_SPACING * 0.5f : 0.0f;
		const int cols = int(radius / MOLOTOV_FLAME_SPACING) + 1;

		for (int col = -cols; col <= cols; col++)
		{
			const float x = col * MOLOTOV_FLAME_SPACING + shift;
			const float d = Q_sqrt(x * x + y * y);

			if (d > radius || m_iNumFlames >= MOLOTOV_MAX_FLAMES)
				continue;

			// the grid turned at random, each point a little off it
			const float jx = x + RANDOM_FLOAT(-6, 6), jy = y + RANDOM_FLOAT(-6, 6);
			const Vector vecTo = vecUp + Vector(jx * c - jy * sn, jx * sn + jy * c, 0);

			// behind a wall: the fire doesn't get there
			UTIL_TraceLine(vecUp, vecTo, ignore_monsters, edict(), &tr);
			if (tr.flFraction < 1.0f || tr.fStartSolid)
				continue;

			// down to the floor, a small step at most: the fire doesn't pour
			// off a ledge
			UTIL_TraceLine(vecTo, vecTo - Vector(0, 0, 40), ignore_monsters, edict(), &tr);
			if (tr.flFraction >= 1.0f || tr.fStartSolid || tr.vecPlaneNormal.z < 0.7f)
				continue;

			if (UTIL_PointContents(tr.vecEndPos + Vector(0, 0, 4)) == CONTENTS_WATER)
				continue;

			// sorted by distance from the centre: the order they light in
			int i = m_iNumFlames++;
			for (; i > 0 && dist[i - 1] > d; i--)
			{
				dist[i] = dist[i - 1];
				m_vecFlames[i] = m_vecFlames[i - 1];
			}

			dist[i] = d;
			m_vecFlames[i] = tr.vecEndPos;
		}
	}
}

// mp_molotov_debug: a red circle where a flame hurts
void CMolotovFire::DrawFlameArea(const Vector &flame)
{
	const int segments = 8;

	for (int i = 0; i < segments; i++)
	{
		const float a0 = i * (M_PI * 2) / segments, a1 = (i + 1) * (M_PI * 2) / segments;
		const Vector p0 = flame + Vector(Q_cos(a0) * MOLOTOV_FLAME_SIZE, Q_sin(a0) * MOLOTOV_FLAME_SIZE, 6);
		const Vector p1 = flame + Vector(Q_cos(a1) * MOLOTOV_FLAME_SIZE, Q_sin(a1) * MOLOTOV_FLAME_SIZE, 6);

		MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
			WRITE_BYTE(TE_BEAMPOINTS);
			WRITE_COORD(p0.x); WRITE_COORD(p0.y); WRITE_COORD(p0.z);
			WRITE_COORD(p1.x); WRITE_COORD(p1.y); WRITE_COORD(p1.z);
			WRITE_SHORT(MODEL_INDEX("sprites/laserbeam.spr"));
			WRITE_BYTE(0);    // start frame
			WRITE_BYTE(0);    // frame rate
			WRITE_BYTE(15);   // life, x 0.1 s: until this flame is drawn again
			WRITE_BYTE(16);   // width
			WRITE_BYTE(0);    // noise
			WRITE_BYTE(255); WRITE_BYTE(0); WRITE_BYTE(0);
			WRITE_BYTE(255);  // brightness
			WRITE_BYTE(0);    // speed
		MESSAGE_END();
	}
}

void CMolotovFire::Start(CBasePlayer *pThrower, const Vector &vecGround)
{
	m_hThrower = pThrower;
	m_iTeam = pThrower ? pThrower->m_iTeam : UNASSIGNED;
	m_vecCenter = vecGround;
	m_flStart = gpGlobals->time;
	m_flEnd = m_flStart + Q_max(0.5f, (m_iTeam == CT ? incendiary_duration.value : molotov_duration.value));
	m_flNextDamage = m_flStart;
	m_flNextSound = m_flStart;
	m_flNextLight = m_flStart;
	m_iLit = 0;
	m_iDebugFlame = 0;

	UTIL_SetOrigin(pev, vecGround);
	pev->owner = pThrower ? pThrower->edict() : nullptr;

	LayFlames();

	EMIT_SOUND(edict(), CHAN_ITEM, "ambience/flameburst1.wav", VOL_NORM, ATTN_NORM);

	SetThink(&CMolotovFire::FireThink);
	pev->nextthink = gpGlobals->time;
}

void CMolotovFire::Extinguish()
{
	for (int i = 0; i < m_iNumFlames; i++)
	{
		if (m_hSprites[i])
			UTIL_Remove(m_hSprites[i]);
	}

	STOP_SOUND(edict(), CHAN_BODY, s_szBurnSounds[0]);
	STOP_SOUND(edict(), CHAN_BODY, s_szBurnSounds[1]);
	STOP_SOUND(edict(), CHAN_BODY, s_szBurnSounds[2]);

	SetThink(&CBaseEntity::SUB_Remove);
	pev->nextthink = gpGlobals->time;
}

// whoever stands in a flame burns, the thrower too
void CMolotovFire::Burn()
{
	CBasePlayer *pThrower = static_cast<CBasePlayer *>((CBaseEntity *)m_hThrower);
	entvars_t *pevAttacker = pThrower ? pThrower->pev : pev;
	const float damage = Q_max(0.0f, molotov_damage.value) * MOLOTOV_DAMAGE_STEP;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);

		if (!pPlayer || !pPlayer->IsAlive() || pPlayer->pev->takedamage == DAMAGE_NO)
			continue;

		const Vector &feet = pPlayer->pev->origin + Vector(0, 0, pPlayer->pev->mins.z);

		for (int j = 0; j < m_iLit; j++)
		{
			const Vector d = feet - m_vecFlames[j];

			if (Q_fabs(d.z) > MOLOTOV_FLAME_HEIGHT || d.Make2D().Length() > MOLOTOV_FLAME_SIZE)
				continue;

			pPlayer->TakeDamage(pev, pevAttacker, damage, DMG_BURN);
			break;
		}
	}
}

void CMolotovFire::FireThink()
{
	pev->nextthink = gpGlobals->time + 0.05f;

	if (gpGlobals->time >= m_flEnd || Molotov_InSmoke(m_vecCenter))
	{
		Extinguish();
		return;
	}

	// spread from the centre
	const int lit = Q_min(m_iNumFlames, int((gpGlobals->time - m_flStart) / MOLOTOV_SPREAD_TIME * m_iNumFlames) + 1);

	for (; m_iLit < lit; m_iLit++)
	{
		// fire.spr is drawn from its middle: its foot on the ground
		const float scale = RANDOM_FLOAT(1.1f, 1.4f);
		CSprite *pSprite = CSprite::SpriteCreate(MOLOTOV_SPRITE, m_vecFlames[m_iLit] + Vector(0, 0, MOLOTOV_SPRITE_HEIGHT * scale * 0.45f), TRUE);

		if (pSprite)
		{
			MAKE_STRING_CLASS(MOLOTOV_FLAME_CLASS, pSprite->pev);
			pSprite->SetTransparency(kRenderTransAdd, 255, 255, 255, 255, kRenderFxNone);
			pSprite->SetScale(scale);
			pSprite->pev->framerate = RANDOM_FLOAT(12.0f, 18.0f);
			pSprite->pev->frame = RANDOM_LONG(0, 10);
			m_hSprites[m_iLit] = pSprite;
		}
	}

	// the pool lights the ground around it, flickering, as far as it burns
	if (gpGlobals->time >= m_flNextLight)
	{
		const float reach = clamp(molotov_radius.value, 32.0f, 400.0f) * Q_min(1.0f, (gpGlobals->time - m_flStart) / MOLOTOV_SPREAD_TIME) + 64.0f;

		MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, m_vecCenter);
			WRITE_BYTE(TE_DLIGHT);
			WRITE_COORD(m_vecCenter.x);
			WRITE_COORD(m_vecCenter.y);
			WRITE_COORD(m_vecCenter.z + 24);
			WRITE_BYTE(int(reach / 10.0f));                    // radius, x 10
			WRITE_BYTE(255); WRITE_BYTE(RANDOM_LONG(100, 140)); WRITE_BYTE(30);
			WRITE_BYTE(2);                                     // life, x 0.1 s
			WRITE_BYTE(0);                                     // decay
		MESSAGE_END();

		m_flNextLight = gpGlobals->time + 0.1f;
	}

	if (gpGlobals->time >= m_flNextSound)
	{
		EMIT_SOUND(edict(), CHAN_BODY, s_szBurnSounds[RANDOM_LONG(0, ARRAYSIZE(s_szBurnSounds) - 1)], VOL_NORM, ATTN_NORM);
		m_flNextSound = gpGlobals->time + 2.5f;
	}

	if (gpGlobals->time >= m_flNextDamage)
	{
		Burn();
		m_flNextDamage = gpGlobals->time + MOLOTOV_DAMAGE_STEP;
	}

	// one flame's area a think, all of them within the circles' life
	if (molotov_debug.value != 0.0f && m_iLit > 0)
	{
		const int perThink = (m_iLit + 19) / 20;

		for (int i = 0; i < perThink; i++)
			DrawFlameArea(m_vecFlames[(m_iDebugFlame++) % m_iLit]);
	}
}

/*
=================
The bottle
=================
*/
class CMolotovProjectile: public CBaseEntity
{
public:
	virtual void Spawn();
	virtual int ObjectCaps() { return FCAP_DONT_SAVE; }

	void EXPORT BottleTouch(CBaseEntity *pOther);
	void EXPORT BottleThink();

	void Burst(bool bOnGround);

	EHANDLE m_hThrower;
	float m_flDetonate;
};

LINK_ENTITY_TO_CLASS(molotov_projectile, CMolotovProjectile, CCSEntity)

void CMolotovProjectile::Spawn()
{
	pev->movetype = MOVETYPE_BOUNCE;
	pev->solid = SOLID_BBOX;

	SET_MODEL(edict(), MOLOTOV_MODEL_WORLD);
	UTIL_SetSize(pev, Vector(-1, -1, -1), Vector(1, 1, 1));

	pev->gravity = clamp(molotov_gravity.value, 0.1f, 3.0f);
	pev->friction = 0.5f;
}

void CMolotovProjectile::Burst(bool bOnGround)
{
	CBasePlayer *pThrower = static_cast<CBasePlayer *>((CBaseEntity *)m_hThrower);
	TraceResult tr;

	EMIT_SOUND(edict(), CHAN_VOICE, s_szBreakSounds[RANDOM_LONG(0, ARRAYSIZE(s_szBreakSounds) - 1)], VOL_NORM, ATTN_NORM);

	// the ground under the burst, when close enough
	UTIL_TraceLine(pev->origin + Vector(0, 0, 8), pev->origin - Vector(0, 0, bOnGround ? 32 : MOLOTOV_AIR_BURST_FIRE), ignore_monsters, edict(), &tr);

	const bool bFire = tr.flFraction < 1.0f && !tr.fStartSolid && tr.vecPlaneNormal.z >= 0.7f
		&& pev->waterlevel == 0 && !Molotov_InSmoke(tr.vecEndPos);

	// a burst of flame where it broke
	MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pev->origin);
		WRITE_BYTE(TE_SPRITE);
		WRITE_COORD(pev->origin.x);
		WRITE_COORD(pev->origin.y);
		WRITE_COORD(pev->origin.z + 16);
		WRITE_SHORT(MODEL_INDEX(MOLOTOV_BURST_SPRITE));
		WRITE_BYTE(bFire ? 12 : 8);  // scale, x 0.1
		WRITE_BYTE(200);             // brightness
	MESSAGE_END();

	if (bFire)
	{
		CMolotovFire *pFire = GetClassPtr<CCSEntity>((CMolotovFire *)nullptr);
		MAKE_STRING_CLASS("molotov", pFire->pev);
		pFire->Spawn();
		pFire->Start(pThrower, tr.vecEndPos);
	}

	UTIL_Remove(this);
}

void CMolotovProjectile::BottleTouch(CBaseEntity *pOther)
{
	// a floor breaks it, walls and players throw it back
	if (pOther->IsBSPModel() && gpGlobals->trace_plane_normal.z >= 0.7f)
	{
		Burst(true);
		return;
	}

	if (pev->flags & FL_ONGROUND)
	{
		Burst(true);
		return;
	}

	pev->velocity = pev->velocity * 0.6f;
	EMIT_SOUND(edict(), CHAN_VOICE, "weapons/grenade_hit1.wav", 0.5f, ATTN_NORM);
}

void CMolotovProjectile::BottleThink()
{
	pev->nextthink = gpGlobals->time + 0.05f;

	if (!IsInWorld())
	{
		UTIL_Remove(this);
		return;
	}

	// in water: the fire never starts
	if (pev->waterlevel > 1)
	{
		EMIT_SOUND(edict(), CHAN_VOICE, s_szBreakSounds[0], VOL_NORM, ATTN_NORM);
		UTIL_Remove(this);
		return;
	}

	if (gpGlobals->time >= m_flDetonate)
		Burst(false);
}

void Molotov_Throw(CBasePlayer *pThrower, const Vector &vecSrc, const Vector &vecVelocity)
{
	CMolotovProjectile *pBottle = GetClassPtr<CCSEntity>((CMolotovProjectile *)nullptr);

	MAKE_STRING_CLASS("molotov_projectile", pBottle->pev);
	pBottle->Spawn();
	SET_MODEL(pBottle->edict(), Molotov_Model('w', pThrower->m_iTeam));
	UTIL_SetSize(pBottle->pev, Vector(-1, -1, -1), Vector(1, 1, 1));

	UTIL_SetOrigin(pBottle->pev, vecSrc);
	pBottle->pev->velocity = vecVelocity;
	pBottle->pev->angles = UTIL_VecToAngles(vecVelocity);
	pBottle->pev->avelocity = Vector(RANDOM_FLOAT(-400, -200), 0, RANDOM_FLOAT(-200, 200));
	pBottle->pev->owner = pThrower->edict();

	pBottle->m_hThrower = pThrower;
	pBottle->m_flDetonate = gpGlobals->time + Q_max(0.2f, molotov_detonate_time.value);

	pBottle->SetTouch(&CMolotovProjectile::BottleTouch);
	pBottle->SetThink(&CMolotovProjectile::BottleThink);
	pBottle->pev->nextthink = gpGlobals->time + 0.05f;
}

// sv_cheats: a molotov bursting on the ground under a point (molotov_here,
// molotov_at), to look at the fire
void Molotov_BurstAt(CBasePlayer *pThrower, const Vector &vecPoint)
{
	CMolotovProjectile *pBottle = GetClassPtr<CCSEntity>((CMolotovProjectile *)nullptr);

	MAKE_STRING_CLASS("molotov_projectile", pBottle->pev);
	pBottle->Spawn();
	UTIL_SetOrigin(pBottle->pev, vecPoint + Vector(0, 0, 4));
	pBottle->pev->owner = pThrower->edict();
	pBottle->m_hThrower = pThrower;
	pBottle->Burst(true);
}

// round restart: no fire, no bottle
void Molotov_CleanUp()
{
	UTIL_RemoveOther("molotov_projectile");
	UTIL_RemoveOther("molotov");
	UTIL_RemoveOther(MOLOTOV_FLAME_CLASS);

	for (auto &smoke : s_smokes)
		smoke.time = 0.0f;
}
