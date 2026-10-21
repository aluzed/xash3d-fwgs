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

void GameModes_RegisterRoundCvars();
void GameModes_RegisterMatchCvars();
void Match_MapStart();

void GameModes_Init()
{
	CVAR_REGISTER(&game_mode);
	CVAR_REGISTER(&csdm_teams);
	GameModes_RegisterRoundCvars();
	GameModes_RegisterMatchCvars();

	ADD_SERVER_COMMAND("sv_mode", GameModes_Command_f);
}

// from the game rules of a new map, after the server's own configs
void GameModes_MapStart()
{
	Match_MapStart();

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

/*
Rounds in free for all (story 059): mp_freeforall 1 with rounds. The teams
only place the players at their spawns; the last player alive wins the
round, a round whose time runs out on more than one player is a draw.
Neither team wins: the money of the round goes to each player, the
winner's to the winner and the loser's to everyone else.
*/

cvar_t ffa_win_money  = { "mp_ffa_win_money", "3250", FCVAR_SERVER, 3250.0f, nullptr };
cvar_t ffa_loss_money = { "mp_ffa_loss_money", "1400", FCVAR_SERVER, 1400.0f, nullptr };

void GameModes_RegisterRoundCvars()
{
	CVAR_REGISTER(&ffa_win_money);
	CVAR_REGISTER(&ffa_loss_money);
}

// a player who plays the round: in a team, with a model chosen
static bool FFA_InRound(CBasePlayer *pPlayer)
{
	return pPlayer && !FNullEnt(pPlayer->edict()) && !pPlayer->has_disconnected
		&& (pPlayer->m_iTeam == TERRORIST || pPlayer->m_iTeam == CT)
		&& pPlayer->m_iJoiningState == JOINED;
}

bool CHalfLifeMultiplay::FreeForAllRoundCheck()
{
	int inGame = 0, alive = 0;
	CBasePlayer *pLastAlive = nullptr;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (!FFA_InRound(pPlayer))
			continue;

		inGame++;
		if (pPlayer->IsAlive())
		{
			alive++;
			pLastAlive = pPlayer;
		}
	}

	// the game starts with two players, whatever their teams
	m_bNeededPlayers = false;
	if (inGame < 2)
	{
		if (m_bGameStarted)
			UTIL_ClientPrintAll(HUD_PRINTCONSOLE, "#Game_scoring");

		m_bNeededPlayers = true;
		m_bGameStarted = false;
		return false;
	}

	if (!m_bGameStarted)
		return OnRoundEnd_Intercept(WINSTATUS_DRAW, ROUND_GAME_COMMENCE, 3);

	if (alive > 1)
		return false;

	FreeForAllRoundEnd(pLastAlive);
	return true;
}

void CHalfLifeMultiplay::FreeForAllRoundEnd(CBasePlayer *pWinner)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (!FFA_InRound(pPlayer))
			continue;

		pPlayer->AddAccount(int(pPlayer == pWinner ? ffa_win_money.value : ffa_loss_money.value), RT_ROUND_BONUS);
	}

	if (pWinner)
	{
		UTIL_LogPrintf("\"%s<%i><%s><%s>\" triggered \"Round_Win\"\n", STRING(pWinner->pev->netname),
			GETPLAYERUSERID(pWinner->edict()), GETPLAYERAUTHID(pWinner->edict()), GetTeam(pWinner->m_iTeam));
		UTIL_LogPrintf("World triggered \"Round_End\"\n");
		UTIL_ClientPrintAll(HUD_PRINTCENTER, "%s1 wins the round!", STRING(pWinner->pev->netname));
		Broadcast(pWinner->m_iTeam == CT ? "ctwin" : "terwin");
		TerminateRound(GetRoundRestartDelay(), WINSTATUS_DRAW);
		return;
	}

	Round_Draw(GetRoundRestartDelay());
}

/*
The match (story 057), the mode "match": warmup until mp_match_players are
in the teams, mp_match_warmup_time more seconds, then the match from a
complete restart. MR12 by default (mp_match_rounds 24): the sides switch
after round 12, the first team to 13 wins; at 12-12 an overtime of
mp_match_ot_rounds (6) with mp_match_ot_money each, sides switched after
its third round, the first to 16 wins, 15-15 is a draw. A switch swaps the
players and the teams' scores (SwapAllPlayers), strips the weapons and
sets everyone's money. At the end the server holds the scoreboard until a
sv_mode, or changes map after mp_match_end_delay seconds.
*/

cvar_t match_players     = { "mp_match_players", "10", FCVAR_SERVER, 10.0f, nullptr };
cvar_t match_warmup_time = { "mp_match_warmup_time", "60", FCVAR_SERVER, 60.0f, nullptr };
cvar_t match_rounds      = { "mp_match_rounds", "24", FCVAR_SERVER, 24.0f, nullptr };
cvar_t match_overtime    = { "mp_match_overtime", "1", FCVAR_SERVER, 1.0f, nullptr };
cvar_t match_ot_rounds   = { "mp_match_ot_rounds", "6", FCVAR_SERVER, 6.0f, nullptr };
cvar_t match_ot_money    = { "mp_match_ot_money", "10000", FCVAR_SERVER, 10000.0f, nullptr };
cvar_t match_end_delay   = { "mp_match_end_delay", "0", FCVAR_SERVER, 0.0f, nullptr };
cvar_t match_allow_bots  = { "mp_match_allow_bots", "0", FCVAR_SERVER, 0.0f, nullptr };

enum MatchState
{
	MATCH_OFF,           // not the match mode
	MATCH_WARMUP,        // waiting for the players
	MATCH_COUNTDOWN,     // the players are there, the last minute of warmup
	MATCH_FIRST_HALF,
	MATCH_SECOND_HALF,
	MATCH_OT_FIRST_HALF,
	MATCH_OT_SECOND_HALF,
	MATCH_ENDED,
};

static MatchState s_MatchState = MATCH_OFF;
static bool s_bWarmupApplied;        // the warmup's settings and restart
static float s_flCountdownEnd;
static float s_flNextWarmupMessage;
static bool s_bRoundEvaluated;       // the scores of the ended round looked at
static bool s_bSwitchSides;          // at the next round's start
static int s_iSetMoney;              // the same, 0: nothing to set

void GameModes_RegisterMatchCvars()
{
	CVAR_REGISTER(&match_players);
	CVAR_REGISTER(&match_warmup_time);
	CVAR_REGISTER(&match_rounds);
	CVAR_REGISTER(&match_overtime);
	CVAR_REGISTER(&match_ot_rounds);
	CVAR_REGISTER(&match_ot_money);
	CVAR_REGISTER(&match_end_delay);
	CVAR_REGISTER(&match_allow_bots);
}

static void Match_Announce(const char *text)
{
	UTIL_ClientPrintAll(HUD_PRINTCENTER, text);
	UTIL_ClientPrintAll(HUD_PRINTTALK, UTIL_VarArgs("%s\n", text));
	UTIL_LogPrintf("Match: %s\n", text);
}

// from the game rules of a new map: a match always starts with its warmup
void Match_MapStart()
{
	const char *mode = GameModes_Current();

	s_MatchState = (mode && !Q_strcmp(mode, "match")) ? MATCH_WARMUP : MATCH_OFF;
	s_bWarmupApplied = false;
	s_flNextWarmupMessage = 0.0f;
	s_bRoundEvaluated = false;
	s_bSwitchSides = false;
	s_iSetMoney = 0;
}

// the players of the match: in a team, bots only for tests
static int Match_CountPlayers()
{
	int count = 0;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (!pPlayer || FNullEnt(pPlayer->edict()) || pPlayer->has_disconnected)
			continue;

		if (pPlayer->m_iTeam != TERRORIST && pPlayer->m_iTeam != CT)
			continue;

		if (pPlayer->IsBot() && !match_allow_bots.value)
			continue;

		count++;
	}

	return count;
}

static void Match_KickBots()
{
	if (match_allow_bots.value)
		return;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (pPlayer && !FNullEnt(pPlayer->edict()) && !pPlayer->has_disconnected && pPlayer->IsBot())
			SERVER_COMMAND(UTIL_VarArgs("kick #%d\n", GETPLAYERUSERID(pPlayer->edict())));
	}
}

static void Match_ApplyWarmup()
{
	// respawn, buy anywhere with all the money, no score: replaced by the
	// mode's config when the match starts
	CVAR_SET_STRING("mp_round_infinite", "1");
	CVAR_SET_FLOAT("mp_forcerespawn", 1.0f);
	CVAR_SET_FLOAT("mp_respawn_immunitytime", 1.0f);
	CVAR_SET_FLOAT("mp_buy_anywhere", 1.0f);
	CVAR_SET_FLOAT("mp_buytime", -1.0f);
	CVAR_SET_FLOAT("mp_startmoney", 16000.0f);
	CVAR_SET_FLOAT("mp_give_player_c4", 0.0f);
	CVAR_SET_FLOAT("mp_freezetime", 0.0f);
	CVAR_SET_FLOAT("sv_restartround", 1.0f);
}

void CHalfLifeMultiplay::MatchThink()
{
	if (s_MatchState == MATCH_OFF || s_MatchState == MATCH_ENDED)
		return;

	// the mode's config has run (queued by the game rules), then the warmup
	if (!s_bWarmupApplied)
	{
		if (gpGlobals->time < 2.0f)
			return;

		s_bWarmupApplied = true;
		Match_ApplyWarmup();
	}

	Match_KickBots();

	if (s_MatchState == MATCH_WARMUP || s_MatchState == MATCH_COUNTDOWN)
	{
		int needed = Q_max(1, int(match_players.value));
		int players = Match_CountPlayers();

		if (players < needed)
		{
			if (s_MatchState == MATCH_COUNTDOWN)
			{
				s_MatchState = MATCH_WARMUP;
				s_flNextWarmupMessage = 0.0f;
			}

			if (gpGlobals->time >= s_flNextWarmupMessage)
			{
				s_flNextWarmupMessage = gpGlobals->time + 10.0f;
				UTIL_ClientPrintAll(HUD_PRINTCENTER, UTIL_VarArgs("Warmup: waiting for players (%d/%d)", players, needed));
			}

			return;
		}

		if (s_MatchState == MATCH_WARMUP)
		{
			s_MatchState = MATCH_COUNTDOWN;
			s_flCountdownEnd = gpGlobals->time + Q_max(0.0f, match_warmup_time.value);
			s_flNextWarmupMessage = 0.0f;
		}

		int left = int(s_flCountdownEnd - gpGlobals->time + 0.5f);
		if (left > 0)
		{
			if (left <= 5 || left % 10 == 0 || gpGlobals->time >= s_flNextWarmupMessage)
			{
				s_flNextWarmupMessage = gpGlobals->time + 10.0f;
				UTIL_ClientPrintAll(HUD_PRINTCENTER, UTIL_VarArgs("Warmup: the match starts in %d s", left));
			}

			return;
		}

		// the match's rules, a complete restart: scores, money, weapons
		s_MatchState = MATCH_FIRST_HALF;
		s_bRoundEvaluated = true;
		SERVER_COMMAND("exec modes/match.cfg\n");
		SERVER_COMMAND("sv_restartround 3\n");
		Match_Announce(UTIL_VarArgs("The match starts: first to %d", Q_max(1, int(match_rounds.value) / 2) + 1));
		return;
	}

	// a round has ended: its scores
	if (m_iRoundWinStatus == WINSTATUS_NONE || s_bRoundEvaluated)
		return;

	s_bRoundEvaluated = true;

	int half = Q_max(1, int(match_rounds.value) / 2);
	int otHalf = Q_max(1, int(match_ot_rounds.value) / 2);
	int played = m_iNumCTWins + m_iNumTerroristWins;
	int best = Q_max(m_iNumCTWins, m_iNumTerroristWins);
	int target = (s_MatchState >= MATCH_OT_FIRST_HALF) ? half + otHalf + 1 : half + 1;
	bool draw = false;

	if (best >= target)
	{
		// a team won
	}
	else if (s_MatchState == MATCH_FIRST_HALF && played == half)
	{
		s_MatchState = MATCH_SECOND_HALF;
		s_bSwitchSides = true;
		s_iSetMoney = int(startmoney.value);
		Match_Announce("Half time, switching sides");
		return;
	}
	else if (s_MatchState == MATCH_SECOND_HALF && played == half * 2)
	{
		if (!match_overtime.value)
			draw = true;
		else
		{
			s_MatchState = MATCH_OT_FIRST_HALF;
			s_iSetMoney = int(match_ot_money.value);
			Match_Announce(UTIL_VarArgs("%d-%d: overtime, first to %d", m_iNumCTWins, m_iNumTerroristWins, half + otHalf + 1));
			return;
		}
	}
	else if (s_MatchState == MATCH_OT_FIRST_HALF && played == half * 2 + otHalf)
	{
		s_MatchState = MATCH_OT_SECOND_HALF;
		s_bSwitchSides = true;
		s_iSetMoney = int(match_ot_money.value);
		Match_Announce("Overtime half time, switching sides");
		return;
	}
	else if (s_MatchState == MATCH_OT_SECOND_HALF && played == half * 2 + otHalf * 2)
	{
		draw = true;
	}
	else
		return;

	// the end of the match
	s_MatchState = MATCH_ENDED;

	if (draw)
		Match_Announce(UTIL_VarArgs("Draw %d-%d", m_iNumCTWins, m_iNumTerroristWins));
	else if (m_iNumCTWins > m_iNumTerroristWins)
		Match_Announce(UTIL_VarArgs("The Counter-Terrorists win the match %d-%d", m_iNumCTWins, m_iNumTerroristWins));
	else
		Match_Announce(UTIL_VarArgs("The Terrorists win the match %d-%d", m_iNumTerroristWins, m_iNumCTWins));

	if (match_end_delay.value > 0.0f)
		CVAR_SET_FLOAT("mp_chattime", match_end_delay.value);

	GoToIntermission();
}

void CHalfLifeMultiplay::MatchRoundRestart()
{
	s_bRoundEvaluated = false;

	if (s_MatchState < MATCH_FIRST_HALF || s_MatchState == MATCH_ENDED)
	{
		s_bSwitchSides = false;
		s_iSetMoney = 0;
		return;
	}

	if (!s_bSwitchSides && !s_iSetMoney)
		return;

	if (s_bSwitchSides)
		SwapAllPlayers();

	// a new start: no bonus of the last round, no losing streak
	m_iAccountTerrorist = m_iAccountCT = 0;
	m_iNumConsecutiveTerroristLoses = m_iNumConsecutiveCTLoses = 0;
	m_iLoserBonus = m_rgRewardAccountRules[RR_LOSER_BONUS_DEFAULT];

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (!pPlayer || FNullEnt(pPlayer->edict()) || pPlayer->has_disconnected)
			continue;

		if (pPlayer->m_iTeam != TERRORIST && pPlayer->m_iTeam != CT)
			continue;

		// respawned with the default weapons and nothing else
		if (s_bSwitchSides)
			pPlayer->m_bNotKilled = false;

		if (s_iSetMoney)
			pPlayer->AddAccount(s_iSetMoney - pPlayer->m_iAccount, RT_NONE);
	}

	s_bSwitchSides = false;
	s_iSetMoney = 0;
}

bool CHalfLifeMultiplay::MatchHoldsIntermission()
{
	return s_MatchState == MATCH_ENDED && match_end_delay.value <= 0.0f;
}
