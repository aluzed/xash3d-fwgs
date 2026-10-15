/*
csdm.cpp - deathmatch rules of the field validation server, Metamod-FWGS plugin
Copyright (C) 2026 aluzed

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// Story 034: what the ReHLDS field server got from CSDM-ReAPI and its own
// AMX Mod X plugin, for the amd64 Xash3D server, which has neither AMX Mod X
// nor ReAPI. Uses ReGameDLL_CS's API directly. The rest of the deathmatch
// (free for all, respawn, protection, equipment) is ReGameDLL_CS cvars
// (csdm.cfg).
//
// - Spawns: the CSDM spawn files (addons/csdm/spawns/<map>.spawns.cfg), a
//   random point other than the player's last one and with no living player
//   within 450 units, like CSDM-ReAPI's spawn manager; the spawn while a
//   player connects keeps the map's spawn points. With spawn files, a team is
//   never full for lack of the map's spawn points (CSDM-ReAPI added half of
//   the file's points to each team's count).
// - On a kill: the killer's active weapon is reloaded (CSDM-ReAPI
//   refill_clip_weapons 1), +15 health, +30 health and +30 armor on a
//   headshot, up to 100, with the medkit sound on a headshot.
//
// cvars: csdm_spawns, csdm_refill_clip, csdm_heal_kill, csdm_heal_headshot,
// csdm_armor_headshot.

#include <precompiled.h>
#include <meta_api.h>
#include <dlfcn.h>
#include <stdio.h>

enginefuncs_t g_engfuncs;
globalvars_t *gpGlobals;
meta_globals_t *gpMetaGlobals;
gamedll_funcs_t *gpGamedllFuncs;
mutil_funcs_t *gpMetaUtilFuncs;

plugin_info_t Plugin_info =
{
	META_INTERFACE_VERSION,
	"CSDM rules",
	"1.0",
	__DATE__,
	"aluzed",
	"https://github.com/aluzed/xash3d-fwgs",
	"CSDM",
	PT_STARTUP,
	PT_NEVER,
};

static IReGameApi *g_ReGameApi;
static const ReGameFuncs_t *g_ReGameFuncs;

static cvar_t cv_spawns = { "csdm_spawns", "1", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_refill_clip = { "csdm_refill_clip", "1", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_heal_kill = { "csdm_heal_kill", "15", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_heal_headshot = { "csdm_heal_headshot", "30", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_armor_headshot = { "csdm_armor_headshot", "30", FCVAR_SERVER, 0.0f, nullptr };

static const char HEAL_SOUND[] = "items/smallmedkit1.wav";
static const float MIN_SPAWN_RADIUS = 450.0f;
static const float MAX_HEALTH = 100.0f;
static const float MAX_ARMOR = 100.0f;

enum { MAX_SPAWNS = 128 };

static struct
{
	Vector origin;
	Vector angles;
} g_Spawns[MAX_SPAWNS];

static int g_NumSpawns;
static int g_LastSpawn[MAX_CLIENTS + 1];
static bool g_FirstSpawn[MAX_CLIENTS + 1];

/*
==================
Spawns
==================
*/
static void ServerPrintf(const char *format, ...)
{
	char text[512];
	va_list args;

	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);

	SERVER_PRINT(text);
}

static void LoadSpawns()
{
	char path[256];
	int length = 0;

	g_NumSpawns = 0;
	snprintf(path, sizeof(path), "addons/csdm/spawns/%s.spawns.cfg", STRING(gpGlobals->mapname));

	byte *file = LOAD_FILE_FOR_ME(path, &length);
	if (!file)
	{
		ServerPrintf("[CSDM] No spawn points file for %s, the map's spawns are used\n", STRING(gpGlobals->mapname));
		return;
	}

	// origin, angles, team (ignored), view angles: 10 numbers per line
	char *line = (char *)file;
	while (line && *line && g_NumSpawns < MAX_SPAWNS)
	{
		char *next = strchr(line, '\n');
		if (next)
			*next++ = '\0';

		float v[10];
		if (line[0] != '/' && line[0] != ';'
			&& sscanf(line, "%f %f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) == 10)
		{
			g_Spawns[g_NumSpawns].origin = Vector(v[0], v[1], v[2]);
			g_Spawns[g_NumSpawns].angles = Vector(v[3], v[4], 0.0f);
			g_NumSpawns++;
		}

		line = next;
	}

	FREE_FILE(file);
	ServerPrintf("[CSDM] Loaded %d spawn points for %s\n", g_NumSpawns, STRING(gpGlobals->mapname));
}

static bool IsAlivePlayer(edict_t *ed)
{
	return ed && !ed->free && ed->pvPrivateData && ed->v.deadflag == DEAD_NO && ed->v.health > 0.0f;
}

// a living player other than this one within MIN_SPAWN_RADIUS
static bool IsSpawnOccupied(int self, const Vector &origin)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		edict_t *ed = INDEXENT(i);

		if (i == self || !IsAlivePlayer(ed))
			continue;

		if ((ed->v.origin - origin).Length() < MIN_SPAWN_RADIUS)
			return true;
	}

	return false;
}

static bool RandomSpawn(CBasePlayer *player)
{
	edict_t *ed = player->edict();
	int index = ENTINDEX(ed);

	if (!cv_spawns.value || g_NumSpawns < 2 || index < 1 || index > MAX_CLIENTS || g_FirstSpawn[index])
		return false;

	// CSDM-ReAPI's walk: random start, then the next points
	int spot = RANDOM_LONG(0, g_NumSpawns - 2);
	for (int attempts = 0; attempts <= g_NumSpawns; attempts++)
	{
		if (spot != g_LastSpawn[index] && !IsSpawnOccupied(index, g_Spawns[spot].origin))
		{
			SET_ORIGIN(ed, g_Spawns[spot].origin);
			ed->v.velocity = Vector(0, 0, 0);
			ed->v.v_angle = Vector(0, 0, 0);
			ed->v.angles = g_Spawns[spot].angles;
			ed->v.punchangle = Vector(0, 0, 0);
			ed->v.fixangle = 1;
			g_LastSpawn[index] = spot;
			return true;
		}

		if (++spot >= g_NumSpawns)
			spot = RANDOM_LONG(0, g_NumSpawns - 2);
	}

	return false;
}

static edict_t *Hook_GetPlayerSpawnSpot(IReGameHook_CSGameRules_GetPlayerSpawnSpot *chain, CBasePlayer *player)
{
	if (RandomSpawn(player))
		return player->edict();

	return chain->callNext(player);
}

static BOOL Hook_TeamFull(IReGameHook_CSGameRules_TeamFull *chain, int team)
{
	if (cv_spawns.value && g_NumSpawns >= 2)
		return FALSE;

	return chain->callNext(team);
}

/*
==================
Kills
==================
*/
static void Hook_PlayerKilled(IReGameHook_CSGameRules_PlayerKilled *chain, CBasePlayer *victim, entvars_t *pevKiller, entvars_t *pevInflictor)
{
	// read before the chain: it may reset the victim
	bool headshot = victim->m_LastHitGroup == HITGROUP_HEAD;

	chain->callNext(victim, pevKiller, pevInflictor);

	if (!pevKiller)
		return;

	edict_t *killerEdict = ENT(pevKiller);
	int killerIndex = ENTINDEX(killerEdict);

	if (killerIndex < 1 || killerIndex > gpGlobals->maxClients || killerEdict == victim->edict() || !IsAlivePlayer(killerEdict))
		return;

	// a player's private data is its CBasePlayer
	CBasePlayer *killer = (CBasePlayer *)killerEdict->pvPrivateData;
	if (!killer)
		return;

	if (cv_refill_clip.value && killer->m_pActiveItem)
		killer->CSPlayer()->ReloadWeapons(killer->m_pActiveItem, true, true);

	float maxHealth = killer->pev->max_health > 0.0f ? killer->pev->max_health : MAX_HEALTH;
	killer->pev->health = Q_min(killer->pev->health + (headshot ? cv_heal_headshot.value : cv_heal_kill.value), maxHealth);

	if (headshot)
	{
		if (cv_armor_headshot.value > 0.0f)
		{
			killer->pev->armorvalue = Q_min(killer->pev->armorvalue + cv_armor_headshot.value, MAX_ARMOR);
			if (killer->m_iKevlar == ARMOR_NONE)
				killer->m_iKevlar = ARMOR_KEVLAR;
		}

		EMIT_SOUND_DYN2(killerEdict, CHAN_ITEM, HEAL_SOUND, VOL_NORM, ATTN_NORM, 0, PITCH_NORM);
	}
}

/*
==================
Game DLL functions
==================
*/
static int DLL_Spawn(edict_t *ed)
{
	// the world spawns first: precache with the map
	if (ed == INDEXENT(0))
		PRECACHE_SOUND(HEAL_SOUND);

	RETURN_META_VALUE(MRES_IGNORED, 0);
}

static void DLL_ServerActivate_Post(edict_t *, int, int)
{
	LoadSpawns();

	for (int i = 0; i <= MAX_CLIENTS; i++)
		g_LastSpawn[i] = -1;

	RETURN_META(MRES_IGNORED);
}

// CSDM-ReAPI: the spawn between the connection and ClientPutInServer keeps
// the map's points
static qboolean DLL_ClientConnect(edict_t *ed, const char *, const char *, char *)
{
	int index = ENTINDEX(ed);

	if (index >= 1 && index <= MAX_CLIENTS)
		g_FirstSpawn[index] = true;

	RETURN_META_VALUE(MRES_IGNORED, TRUE);
}

static void DLL_ClientPutInServer(edict_t *ed)
{
	int index = ENTINDEX(ed);

	if (index >= 1 && index <= MAX_CLIENTS)
	{
		g_LastSpawn[index] = -1;
		g_FirstSpawn[index] = false;
	}

	RETURN_META(MRES_IGNORED);
}

static DLL_FUNCTIONS g_DllFunctions;
static DLL_FUNCTIONS g_DllFunctionsPost;

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS *table, int *)
{
	g_DllFunctions.pfnSpawn = DLL_Spawn;
	g_DllFunctions.pfnClientConnect = DLL_ClientConnect;
	g_DllFunctions.pfnClientPutInServer = DLL_ClientPutInServer;
	memcpy(table, &g_DllFunctions, sizeof(DLL_FUNCTIONS));
	return TRUE;
}

C_DLLEXPORT int GetEntityAPI2_Post(DLL_FUNCTIONS *table, int *)
{
	g_DllFunctionsPost.pfnServerActivate = DLL_ServerActivate_Post;
	memcpy(table, &g_DllFunctionsPost, sizeof(DLL_FUNCTIONS));
	return TRUE;
}

/*
==================
Metamod
==================
*/
static bool ReGameApi_Init()
{
	const char *path = GET_GAME_INFO(PLID, GINFO_DLL_FULLPATH);
	void *module = path ? dlopen(path, RTLD_NOW | RTLD_NOLOAD) : nullptr;

	if (!module)
		return false;

	CreateInterfaceFn factory = (CreateInterfaceFn)dlsym(module, CREATEINTERFACE_PROCNAME);
	dlclose(module); // the game DLL stays loaded: RTLD_NOLOAD only took a reference

	if (!factory)
		return false;

	int ret = 0;
	g_ReGameApi = (IReGameApi *)factory(VRE_GAMEDLL_API_VERSION, &ret);
	if (!g_ReGameApi)
		return false;

	if (g_ReGameApi->GetMajorVersion() != REGAMEDLL_API_VERSION_MAJOR || g_ReGameApi->GetMinorVersion() < REGAMEDLL_API_VERSION_MINOR)
	{
		ServerPrintf("[CSDM] ReGameDLL API %d.%d, built for %d.%d\n",
			g_ReGameApi->GetMajorVersion(), g_ReGameApi->GetMinorVersion(), REGAMEDLL_API_VERSION_MAJOR, REGAMEDLL_API_VERSION_MINOR);
		return false;
	}

	g_ReGameFuncs = g_ReGameApi->GetFuncs();
	return true;
}

C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t *engfuncs, globalvars_t *globals)
{
	memcpy(&g_engfuncs, engfuncs, sizeof(enginefuncs_t));
	gpGlobals = globals;
}

C_DLLEXPORT int Meta_Query(char *, plugin_info_t **info, mutil_funcs_t *utilFuncs)
{
	*info = &Plugin_info;
	gpMetaUtilFuncs = utilFuncs;
	return TRUE;
}

static META_FUNCTIONS g_MetaFunctions =
{
	nullptr,
	nullptr,
	GetEntityAPI2,
	GetEntityAPI2_Post,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
};

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME, META_FUNCTIONS *table, meta_globals_t *globals, gamedll_funcs_t *gamedllFuncs)
{
	gpMetaGlobals = globals;
	gpGamedllFuncs = gamedllFuncs;

	if (!ReGameApi_Init())
	{
		SERVER_PRINT("[CSDM] ReGameDLL API not found, plugin not loaded\n");
		return FALSE;
	}

	CVAR_REGISTER(&cv_spawns);
	CVAR_REGISTER(&cv_refill_clip);
	CVAR_REGISTER(&cv_heal_kill);
	CVAR_REGISTER(&cv_heal_headshot);
	CVAR_REGISTER(&cv_armor_headshot);

	IReGameHookchains *hooks = g_ReGameApi->GetHookchains();
	hooks->CSGameRules_GetPlayerSpawnSpot()->registerHook(Hook_GetPlayerSpawnSpot);
	hooks->CSGameRules_TeamFull()->registerHook(Hook_TeamFull);
	hooks->CSGameRules_PlayerKilled()->registerHook(Hook_PlayerKilled);

	memcpy(table, &g_MetaFunctions, sizeof(META_FUNCTIONS));
	return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME, PL_UNLOAD_REASON)
{
	return TRUE;
}
