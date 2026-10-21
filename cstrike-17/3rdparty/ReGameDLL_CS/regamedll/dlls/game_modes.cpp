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
void GameModes_RegisterCsdmCvars();
void Match_MapStart();
void Csdm_MapStart();

void GameModes_Init()
{
	CVAR_REGISTER(&game_mode);
	CVAR_REGISTER(&csdm_teams);
	GameModes_RegisterRoundCvars();
	GameModes_RegisterMatchCvars();
	GameModes_RegisterCsdmCvars();

	ADD_SERVER_COMMAND("sv_mode", GameModes_Command_f);
}

// from the game rules of a new map, after the server's own configs
void GameModes_MapStart()
{
	Match_MapStart();
	Csdm_MapStart();

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
	MATCH_ENDING,        // the winners' interlude
	MATCH_ENDED,
};

// the interludes of story 058, at the start of a round
enum InterludeKind
{
	INTERLUDE_NONE = -1,
	INTERLUDE_START,
	INTERLUDE_HALF_TIME,
	INTERLUDE_OVERTIME,
	INTERLUDE_OVERTIME_SWITCH,
	INTERLUDE_END,
};

static MatchState s_MatchState = MATCH_OFF;
static bool s_bWarmupApplied;        // the warmup's settings and restart
static float s_flCountdownEnd;
static float s_flNextWarmupMessage;
static bool s_bRoundEvaluated;       // the scores of the ended round looked at
static bool s_bSwitchSides;          // at the next round's start
static int s_iSetMoney;              // the same, 0: nothing to set
static InterludeKind s_PendingInterlude = INTERLUDE_NONE;
static int s_iWinningTeam;           // at the end: TERRORIST, CT, or 0 on a draw

void GameModes_RegisterInterludeCvars();

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
	GameModes_RegisterInterludeCvars();
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
	s_PendingInterlude = INTERLUDE_NONE;
	s_iWinningTeam = 0;
}

static void Interlude_Clear();
static void Interlude_Fallback(CHalfLifeMultiplay *pRules);
void GameModes_RegisterInterludeCvars();

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
	if (s_MatchState == MATCH_ENDING)
		Interlude_Fallback(this);

	if (s_MatchState == MATCH_OFF || s_MatchState == MATCH_ENDED || s_MatchState == MATCH_ENDING)
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
		s_PendingInterlude = INTERLUDE_START;
		m_bGameStarted = true; // no "game commencing" restart over the interlude
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
		s_PendingInterlude = INTERLUDE_HALF_TIME;
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
			s_PendingInterlude = INTERLUDE_OVERTIME;
			Match_Announce(UTIL_VarArgs("%d-%d: overtime, first to %d", m_iNumCTWins, m_iNumTerroristWins, half + otHalf + 1));
			return;
		}
	}
	else if (s_MatchState == MATCH_OT_FIRST_HALF && played == half * 2 + otHalf)
	{
		s_MatchState = MATCH_OT_SECOND_HALF;
		s_bSwitchSides = true;
		s_iSetMoney = int(match_ot_money.value);
		s_PendingInterlude = INTERLUDE_OVERTIME_SWITCH;
		Match_Announce("Overtime half time, switching sides");
		return;
	}
	else if (s_MatchState == MATCH_OT_SECOND_HALF && played == half * 2 + otHalf * 2)
	{
		draw = true;
	}
	else
		return;

	// the end of the match: the winners' interlude at the next round's
	// start, then the intermission
	s_MatchState = MATCH_ENDING;
	s_PendingInterlude = INTERLUDE_END;
	s_iWinningTeam = draw ? 0 : (m_iNumCTWins > m_iNumTerroristWins ? CT : TERRORIST);

	if (draw)
		Match_Announce(UTIL_VarArgs("Draw %d-%d", m_iNumCTWins, m_iNumTerroristWins));
	else if (m_iNumCTWins > m_iNumTerroristWins)
		Match_Announce(UTIL_VarArgs("The Counter-Terrorists win the match %d-%d", m_iNumCTWins, m_iNumTerroristWins));
	else
		Match_Announce(UTIL_VarArgs("The Terrorists win the match %d-%d", m_iNumTerroristWins, m_iNumCTWins));

	if (match_end_delay.value > 0.0f)
		CVAR_SET_FLOAT("mp_chattime", match_end_delay.value);
}

void CHalfLifeMultiplay::MatchRoundRestart()
{
	s_bRoundEvaluated = false;
	Interlude_Clear();

	if (s_MatchState < MATCH_FIRST_HALF || s_MatchState >= MATCH_ENDING)
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

/*
The match's interludes (story 058): at the match's start, half time, the
start of overtime and its switch, the round starts with
mp_match_interlude_time (6) more seconds of freeze during which the players
stand still in their spawns, inputs ignored (no turning, shooting, buying,
weapon switch); everyone's view is the interlude's camera, drawn by the
client from the Interlude message (the players shown, their positions and
facing). At the end of the match the winners are shown the same way, the
losers hidden, then the intermission. The shown players are sent to every
client whatever their PVS (EF_FORCEVISIBILITY), the camera being far from
the viewer.
*/

cvar_t match_interlude_time = { "mp_match_interlude_time", "6", FCVAR_SERVER, 6.0f, nullptr };

enum { INTERLUDE_MAX_SHOWN = 20 };  // the message's size

static InterludeKind s_Interlude = INTERLUDE_NONE;
static float s_flInterludeEnd;
static bool s_bShown[MAX_CLIENTS + 1];
static bool s_bHidden[MAX_CLIENTS + 1];
static Vector s_vecHeldAngles[MAX_CLIENTS + 1];

void GameModes_RegisterInterludeCvars()
{
	CVAR_REGISTER(&match_interlude_time);
}

static void Interlude_Clear()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);

		if (pPlayer && !FNullEnt(pPlayer->edict()))
		{
			if (s_bShown[i])
				pPlayer->pev->effects &= ~EF_FORCEVISIBILITY;
		}

		s_bShown[i] = false;
		s_bHidden[i] = false;
	}

	s_Interlude = INTERLUDE_NONE;
}

bool GameModes_InterludeActive()
{
	return s_Interlude != INTERLUDE_NONE;
}

bool GameModes_InterludeBlocksCommand(const char *pcmd)
{
	if (s_Interlude == INTERLUDE_NONE || !pcmd)
		return false;

	static const char *s_Blocked[] = { "lastinv", "drop", "jointeam", "chooseteam", "buy", "buyequip", "buyammo1", "buyammo2",
		"primammo", "secammo", "cl_autobuy", "cl_rebuy", "nightvision", "radio1", "radio2", "radio3" };

	if (!Q_strnicmp(pcmd, "weapon_", 7))
		return true;

	for (auto cmd : s_Blocked)
	{
		if (!Q_stricmp(pcmd, cmd))
			return true;
	}

	return false;
}

// the interlude is over: the round goes on, or the match ends
static void Interlude_Finish(CHalfLifeMultiplay *pRules)
{
	InterludeKind kind = s_Interlude;
	Interlude_Clear();

	if (kind == INTERLUDE_END && s_MatchState == MATCH_ENDING)
	{
		s_MatchState = MATCH_ENDED;
		pRules->GoToIntermission();
	}
}

void CHalfLifeMultiplay::MatchRoundStarted()
{
	if (s_PendingInterlude == INTERLUDE_NONE)
		return;

	InterludeKind kind = s_PendingInterlude;
	s_PendingInterlude = INTERLUDE_NONE;

	float duration = Q_max(1.0f, Q_min(25.0f, match_interlude_time.value));

	s_Interlude = kind;
	s_flInterludeEnd = gpGlobals->time + duration;

	// the round's freeze starts after the interlude
	if (kind != INTERLUDE_END)
	{
		m_iIntroRoundTime += int(duration);
		m_iRoundTimeSecs = m_iIntroRoundTime;
		SyncRoundTimerForAll();
	}

	CBasePlayer *pShown[INTERLUDE_MAX_SHOWN];
	int count = 0;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		s_bShown[i] = s_bHidden[i] = false;

		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (!pPlayer || FNullEnt(pPlayer->edict()) || pPlayer->has_disconnected || !pPlayer->IsAlive())
			continue;

		if (pPlayer->m_iTeam != TERRORIST && pPlayer->m_iTeam != CT)
			continue;

		// the end shows the winners only, both teams on a draw
		if (kind == INTERLUDE_END && s_iWinningTeam && pPlayer->m_iTeam != s_iWinningTeam)
		{
			s_bHidden[i] = true;
			pPlayer->pev->effects |= EF_NODRAW;
			pPlayer->pev->solid = SOLID_NOT;
			pPlayer->pev->takedamage = DAMAGE_NO;
			continue;
		}

		s_bShown[i] = true;
		s_vecHeldAngles[i] = pPlayer->pev->angles;
		s_vecHeldAngles[i].x = 0.0f;
		pPlayer->pev->effects |= EF_FORCEVISIBILITY;

		if (count < INTERLUDE_MAX_SHOWN)
			pShown[count++] = pPlayer;
	}

	MESSAGE_BEGIN(MSG_ALL, gmsgInterlude);
		WRITE_BYTE(kind);
		WRITE_BYTE(int(duration * 10.0f));
		WRITE_BYTE(count);
		for (int i = 0; i < count; i++)
		{
			WRITE_BYTE(pShown[i]->entindex());
			WRITE_BYTE(pShown[i]->m_iTeam);
			WRITE_COORD(pShown[i]->pev->origin.x);
			WRITE_COORD(pShown[i]->pev->origin.y);
			WRITE_COORD(pShown[i]->pev->origin.z);
			WRITE_ANGLE(pShown[i]->pev->angles.y);
		}
	MESSAGE_END();

	UTIL_LogPrintf("Match: interlude %d, %d players shown\n", int(kind), count);
}

// every frame for each player: held still while the interlude lasts
void CHalfLifeMultiplay::MatchPlayerThink(CBasePlayer *pPlayer)
{
	if (s_Interlude == INTERLUDE_NONE)
		return;

	if (gpGlobals->time >= s_flInterludeEnd)
	{
		Interlude_Finish(this);
		return;
	}

	int i = pPlayer->entindex();
	if (i < 1 || i > MAX_CLIENTS)
		return;

	pPlayer->pev->button = 0;
	pPlayer->m_afButtonPressed = 0;
	pPlayer->pev->velocity = g_vecZero;

	if (s_bShown[i])
	{
		pPlayer->pev->angles = s_vecHeldAngles[i];
		pPlayer->pev->v_angle = s_vecHeldAngles[i];
		pPlayer->pev->fixangle = 1;
	}
}

// without players to think, the end's interlude still finishes
static void Interlude_Fallback(CHalfLifeMultiplay *pRules)
{
	if (s_Interlude != INTERLUDE_NONE)
	{
		if (gpGlobals->time >= s_flInterludeEnd + 1.0f)
			Interlude_Finish(pRules);
	}
	else if (s_PendingInterlude == INTERLUDE_NONE && s_MatchState == MATCH_ENDING)
	{
		s_MatchState = MATCH_ENDED;
		pRules->GoToIntermission();
	}
}

/*
The deathmatch's spawns and rewards (story 067), in the game library: what
the Metamod plugin of story 034 (scripts/server/csdm) did for the Linux
server, for every server, the installer's on Windows included. Only in the
mode "csdm"; the plugin leaves them to the game library when it finds
these cvars.

- Spawns (mp_csdm_spawns): the CSDM spawn file of the map
  (addons/csdm/spawns/<map>.spawns.cfg, CSDM-ReAPI's), else in free for all
  every spawn point of the map, both teams'; a random one other than the
  player's last, with no living player within 450 units (in teams, no
  enemy). A player joining keeps the map's spawn points, and so does team
  deathmatch without a spawn file.
- On a kill: the killer's weapon reloaded (mp_csdm_refill_clip), health
  (mp_csdm_heal_kill, mp_csdm_heal_headshot) and on a headshot armor
  (mp_csdm_armor_headshot), up to 100, the medkit's sound on a headshot.
*/

cvar_t csdm_spawns          = { "mp_csdm_spawns", "1", FCVAR_SERVER, 1.0f, nullptr };
cvar_t csdm_refill_clip     = { "mp_csdm_refill_clip", "1", FCVAR_SERVER, 1.0f, nullptr };
cvar_t csdm_heal_kill       = { "mp_csdm_heal_kill", "15", FCVAR_SERVER, 15.0f, nullptr };
cvar_t csdm_heal_headshot   = { "mp_csdm_heal_headshot", "30", FCVAR_SERVER, 30.0f, nullptr };
cvar_t csdm_armor_headshot  = { "mp_csdm_armor_headshot", "30", FCVAR_SERVER, 30.0f, nullptr };

static const float CSDM_SPAWN_RADIUS = 450.0f;
static const char CSDM_HEAL_SOUND[] = "items/smallmedkit1.wav";

enum { CSDM_MAX_SPAWNS = 128 };

static struct
{
	Vector origin;
	Vector angles;
} s_CsdmSpawns[CSDM_MAX_SPAWNS];

static int s_iCsdmSpawns = -1;      // -1: not looked for on this map yet
static bool s_bCsdmSpawnFile;       // from a spawn file, not the map's points
static int s_iCsdmLastSpawn[MAX_CLIENTS + 1];

void GameModes_RegisterCsdmCvars()
{
	CVAR_REGISTER(&csdm_spawns);
	CVAR_REGISTER(&csdm_refill_clip);
	CVAR_REGISTER(&csdm_heal_kill);
	CVAR_REGISTER(&csdm_heal_headshot);
	CVAR_REGISTER(&csdm_armor_headshot);
}

void Csdm_MapStart()
{
	s_iCsdmSpawns = -1;
	s_bCsdmSpawnFile = false;

	for (int i = 0; i <= MAX_CLIENTS; i++)
		s_iCsdmLastSpawn[i] = -1;
}

static bool Csdm_Active()
{
	const char *mode = GameModes_Current();
	return mode && !Q_strcmp(mode, "csdm");
}

static void Csdm_AddSpawn(const Vector &origin, const Vector &angles)
{
	if (s_iCsdmSpawns >= CSDM_MAX_SPAWNS)
		return;

	s_CsdmSpawns[s_iCsdmSpawns].origin = origin;
	s_CsdmSpawns[s_iCsdmSpawns].angles = angles;
	s_iCsdmSpawns++;
}

static void Csdm_LoadSpawns()
{
	s_iCsdmSpawns = 0;

	char path[256];
	Q_snprintf(path, sizeof(path), "addons/csdm/spawns/%s.spawns.cfg", STRING(gpGlobals->mapname));

	int length = 0;
	byte *file = LOAD_FILE_FOR_ME(path, &length);
	if (file)
	{
		// origin, angles, team (ignored), view angles: 10 numbers per line
		char *line = (char *)file;
		while (line && *line && s_iCsdmSpawns < CSDM_MAX_SPAWNS)
		{
			char *next = Q_strchr(line, '\n');
			if (next)
				*next++ = '\0';

			float v[10];
			if (line[0] != '/' && line[0] != ';'
				&& sscanf(line, "%f %f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) == 10)
			{
				Csdm_AddSpawn(Vector(v[0], v[1], v[2]), Vector(v[3], v[4], 0.0f));
			}

			line = next;
		}

		FREE_FILE(file);
		s_bCsdmSpawnFile = true;
		CONSOLE_ECHO("CSDM: %d spawn points from %s\n", s_iCsdmSpawns, path);
		return;
	}

	// both teams' points of the map
	static const char *s_SpawnClasses[] = { "info_player_start", "info_player_deathmatch" };
	for (auto classname : s_SpawnClasses)
	{
		CBaseEntity *pSpot = nullptr;
		while ((pSpot = UTIL_FindEntityByClassname(pSpot, classname)))
			Csdm_AddSpawn(pSpot->pev->origin, pSpot->pev->angles);
	}

	CONSOLE_ECHO("CSDM: no spawn file for %s, its %d spawn points\n", STRING(gpGlobals->mapname), s_iCsdmSpawns);
}

// a living player other than this one, in teams an enemy, near the point
static bool Csdm_SpawnOccupied(CBasePlayer *pPlayer, const Vector &origin, bool teams)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pOther = UTIL_PlayerByIndex(i);
		if (!pOther || pOther == pPlayer || FNullEnt(pOther->edict()) || pOther->has_disconnected || !pOther->IsAlive())
			continue;

		if (teams && pOther->m_iTeam == pPlayer->m_iTeam)
			continue;

		if ((pOther->pev->origin - origin).Length() < CSDM_SPAWN_RADIUS)
			return true;
	}

	return false;
}

// places the player at a deathmatch spawn point, false to leave it to the map's
bool Csdm_SpawnPlayer(CBasePlayer *pPlayer)
{
	if (!Csdm_Active() || !csdm_spawns.value)
		return false;

	if (pPlayer->m_iTeam != TERRORIST && pPlayer->m_iTeam != CT)
		return false;

	if (s_iCsdmSpawns < 0)
		Csdm_LoadSpawns();

	const bool teams = !CSGameRules()->IsFreeForAll();
	if (s_iCsdmSpawns < 2 || (teams && !s_bCsdmSpawnFile))
		return false;

	int index = pPlayer->entindex();
	if (index < 1 || index > MAX_CLIENTS)
		return false;

	// CSDM-ReAPI's walk: a random start, then the next points
	int spot = RANDOM_LONG(0, s_iCsdmSpawns - 1);
	for (int attempts = 0; attempts < s_iCsdmSpawns; attempts++, spot = (spot + 1) % s_iCsdmSpawns)
	{
		if (spot == s_iCsdmLastSpawn[index] || Csdm_SpawnOccupied(pPlayer, s_CsdmSpawns[spot].origin, teams))
			continue;

		pPlayer->pev->origin = s_CsdmSpawns[spot].origin + Vector(0, 0, 1);
		UTIL_SetOrigin(pPlayer->pev, pPlayer->pev->origin);
		pPlayer->pev->velocity = g_vecZero;
		pPlayer->pev->v_angle = g_vecZero;
		pPlayer->pev->angles = s_CsdmSpawns[spot].angles;
		pPlayer->pev->punchangle = g_vecZero;
		pPlayer->pev->fixangle = 1;
		s_iCsdmLastSpawn[index] = spot;
		return true;
	}

	return false;
}

void Csdm_PlayerKilled(CBasePlayer *pVictim, entvars_t *pevKiller, bool headshot)
{
	if (!Csdm_Active() || !pevKiller)
		return;

	CBaseEntity *pKillerEntity = CBaseEntity::Instance(pevKiller);
	if (!pKillerEntity || !pKillerEntity->IsPlayer() || pKillerEntity == pVictim || !pKillerEntity->IsAlive())
		return;

	CBasePlayer *pKiller = static_cast<CBasePlayer *>(pKillerEntity);

	if (csdm_refill_clip.value && pKiller->m_pActiveItem)
		pKiller->ReloadWeapons(pKiller->m_pActiveItem, true, true);

	float maxHealth = pKiller->pev->max_health > 0.0f ? pKiller->pev->max_health : 100.0f;
	pKiller->pev->health = Q_min(pKiller->pev->health + (headshot ? csdm_heal_headshot.value : csdm_heal_kill.value), maxHealth);

	if (headshot)
	{
		if (csdm_armor_headshot.value > 0.0f)
		{
			pKiller->pev->armorvalue = Q_min(pKiller->pev->armorvalue + csdm_armor_headshot.value, 100.0f);
			if (pKiller->m_iKevlar == ARMOR_NONE)
				pKiller->m_iKevlar = ARMOR_KEVLAR;
		}

		EMIT_SOUND(pKiller->edict(), CHAN_ITEM, CSDM_HEAL_SOUND, VOL_NORM, ATTN_NORM);
	}
}
