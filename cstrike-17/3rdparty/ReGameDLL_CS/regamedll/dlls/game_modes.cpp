#include "precompiled.h"

/*
The server's game modes (story 056 of the xash3d-fwgs fork)

mp_mode names the mode the server plays: match (MR13, story 057), ffa
(rounds, everyone against everyone, 059) or csdm (deathmatch with respawn,
060, in free for all or, with mp_csdm_teams 1, in teams). Each mode is a
config of its own in modes/ (extras.pk3) that sets every cvar any mode
sets, so that the order of the switches never matters. Empty, the server
plays as its configs say, as before.

sv_mode <match|ffa|csdm> [ffa|team] switches: it sets the mode, executes
its config and restarts the map, so that nothing of the previous mode stays
(scores, money, weapons on the ground). The game rules execute the mode's
config again on every map, after server.cfg: the mode stays over a map
change.
*/

cvar_t game_mode  = { "mp_mode", "", FCVAR_SERVER, 0.0f, nullptr };
cvar_t csdm_teams = { "mp_csdm_teams", "0", FCVAR_SERVER, 0.0f, nullptr };

struct GameMode_t
{
	const char *name;
	const char *title;
};

static const GameMode_t s_Modes[] =
{
	{ "match", "match (MR13)" },
	{ "ffa",   "free for all" },
	{ "csdm",  "deathmatch" },
};

// set by sv_mode, cleared once the players of the new map are told
static bool s_bAnnounce = false;

static const GameMode_t *GameModes_Find(const char *name)
{
	for (auto &mode : s_Modes)
	{
		if (!Q_stricmp(mode.name, name))
			return &mode;
	}

	return nullptr;
}

const char *GameModes_Current()
{
	const GameMode_t *mode = GameModes_Find(game_mode.string);
	return mode ? mode->name : nullptr;
}

static const char *GameModes_Title()
{
	const GameMode_t *mode = GameModes_Find(game_mode.string);
	if (!mode)
		return nullptr;

	if (!Q_strcmp(mode->name, "csdm"))
		return csdm_teams.value ? "team deathmatch" : "deathmatch, free for all";

	return mode->title;
}

static const char *GameModes_ConfigFile()
{
	const char *mode = GameModes_Current();
	if (!mode)
		return nullptr;

	if (!Q_strcmp(mode, "csdm"))
		return csdm_teams.value ? "modes/csdm_team.cfg" : "modes/csdm_ffa.cfg";

	// not UTIL_VarArgs: its buffer is the one the callers format into
	static char szFile[64];
	Q_snprintf(szFile, sizeof(szFile), "modes/%s.cfg", mode);
	return szFile;
}

static void GameModes_PrintList()
{
	const char *title = GameModes_Title();

	SERVER_PRINT(UTIL_VarArgs("Mode: %s\n", title ? title : "none (the server's own configs)"));
	SERVER_PRINT("usage: sv_mode <mode> [ffa|team]\n"
		"  match      competitive match, MR13 with overtime, players only\n"
		"  ffa        rounds, everyone against everyone\n"
		"  csdm ffa   deathmatch with respawn, everyone against everyone\n"
		"  csdm team  deathmatch with respawn, terrorists against counter-terrorists\n");
}

static void GameModes_Command_f()
{
	if (CMD_ARGC() < 2)
	{
		GameModes_PrintList();
		return;
	}

	const GameMode_t *mode = GameModes_Find(CMD_ARGV(1));
	if (!mode)
	{
		SERVER_PRINT(UTIL_VarArgs("sv_mode: unknown mode \"%s\"\n", CMD_ARGV(1)));
		GameModes_PrintList();
		return;
	}

	float teams = csdm_teams.value;
	if (CMD_ARGC() >= 3)
	{
		const char *form = CMD_ARGV(2);

		if (Q_strcmp(mode->name, "csdm") != 0)
		{
			SERVER_PRINT(UTIL_VarArgs("sv_mode: %s has no form \"%s\"\n", mode->name, form));
			return;
		}

		if (!Q_stricmp(form, "team"))
			teams = 1.0f;
		else if (!Q_stricmp(form, "ffa"))
			teams = 0.0f;
		else
		{
			SERVER_PRINT(UTIL_VarArgs("sv_mode: unknown form \"%s\", ffa or team\n", form));
			return;
		}
	}

	CVAR_SET_STRING("mp_mode", mode->name);
	CVAR_SET_FLOAT("mp_csdm_teams", teams);

	SERVER_PRINT(UTIL_VarArgs("Mode: %s\n", GameModes_Title()));

	// no map yet (the command line): the first map's game rules execute it
	if (!g_pGameRules || FStringNull(gpGlobals->mapname))
		return;

	s_bAnnounce = true;
	SERVER_COMMAND(UTIL_VarArgs("exec %s\n", GameModes_ConfigFile()));
	SERVER_COMMAND(UTIL_VarArgs("changelevel %s\n", STRING(gpGlobals->mapname)));
}

void GameModes_Init()
{
	CVAR_REGISTER(&game_mode);
	CVAR_REGISTER(&csdm_teams);

	ADD_SERVER_COMMAND("sv_mode", GameModes_Command_f);
}

// from the game rules of a new map, after the server's own configs
void GameModes_MapStart()
{
	const char *cfg = GameModes_ConfigFile();
	if (!cfg)
		return;

	ALERT(at_console, "Executing the mode's config file %s\n", cfg);
	SERVER_COMMAND(UTIL_VarArgs("exec %s\n", cfg));
}

// once a second: tells the players of the map a switch restarted
void GameModes_Think()
{
	if (!s_bAnnounce || gpGlobals->time < 6.0f)
		return;

	const char *title = GameModes_Title();
	if (!title)
	{
		s_bAnnounce = false;
		return;
	}

	bool players = false;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (pPlayer && !FNullEnt(pPlayer->edict()) && !pPlayer->IsBot() && !pPlayer->has_disconnected)
		{
			players = true;
			break;
		}
	}

	if (!players)
		return;

	s_bAnnounce = false;
	UTIL_ClientPrintAll(HUD_PRINTCENTER, "Mode: %s1", title);
	UTIL_ClientPrintAll(HUD_PRINTTALK, "Mode: %s1\n", title);
}
