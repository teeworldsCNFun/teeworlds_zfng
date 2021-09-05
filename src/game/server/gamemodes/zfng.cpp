/* (c) KeksTW    */
#include "zfng.h"
#include <game/mapitems.h>
#include "../entities/character.h"
#include "../entities/flag.h"
#include "../entities/flag_stand.h"
#include "../player.h"
#include "../fng2define.h"
#include <engine/shared/config.h>
#include <string.h>
#include <stdio.h>

#define TICK_SPEED Server()->TickSpeed()
#define TICK Server()->Tick()

CGameControllerZFNG::CGameControllerZFNG(class CGameContext *pGameServer) :
	IGameController((class CGameContext*)pGameServer),
	m_Broadcaster(pGameServer)
{
	m_pGameType = "zfng";
	m_GameFlags = GAMEFLAG_FLAGS;
	m_Nuke = NULL;
	m_pFlag = NULL;
	SetGameState(IGS_WAITING_FOR_PLAYERS);
}

CGameControllerZFNG::CGameControllerZFNG(
	class CGameContext *pGameServer,
	CConfiguration& pConfig
) :
	IGameController((class CGameContext*)pGameServer, pConfig),
	m_Broadcaster(pGameServer)
{
	m_pGameType = "zfng";
	m_GameFlags = GAMEFLAG_FLAGS;
	m_Nuke = NULL;
	m_pFlag = NULL;
	SetGameState(IGS_WAITING_FOR_PLAYERS);
}

bool CGameControllerZFNG::IsTeamplay() const
{
	return true;
}

bool CGameControllerZFNG::UseFakeTeams()
{
	return true;
}

bool CGameControllerZFNG::IsInfection() const
{
	return true;
}

void CGameControllerZFNG::SetGameState(EGameState GameState)
{
	m_GameState = GameState;

	switch (GameState)
	{
		case IGS_WAITING_FOR_PLAYERS:
			m_GameStateTimer = TIMER_INFINITE;
			break;
		case IGS_WAITING_FOR_INFECTION:
			m_GameStateTimer = TICK_SPEED * m_Config.m_SvZFNGInfectionDelay;
			IGameController::StartRound();
			break;
		case IGS_WAITING_FLAG:
			m_GameStateTimer = TICK_SPEED * m_Config.m_SvZFNGNukeDelay;
			break;
		case IGS_NORMAL:
			SpawnFlag();
			AnnounceNuke();
			m_GameStateTimer = TIMER_INFINITE;
			break;
		case IGS_NUKE_DETONATED:
			m_Nuke = new CNuke(
				GameServer(),
				m_aFlagStandPositions[NukeDetonatorTeam()]
			);
			m_GameStateTimer = TIMER_INFINITE;
			break;
		case IGS_FINISHING_OFF_ZOMBIES:
			RemoveNuke();
			FinishOffZombies();
			m_GameStateTimer = TICK_SPEED * 2;
			break;
		case IGS_ROUND_ENDED:
			// We don't do anything here, because we rely on `m_GameOverTick`
			// from `IGameController`
			m_GameStateTimer = TIMER_INFINITE;
			break;
	}
}

void CGameControllerZFNG::Tick()
{
	if (m_GameOverTick != -1) {
		if (TICK > m_GameOverTick + TICK_SPEED * m_Config.m_SvDelayBetweenRounds) {
			CycleMap();
			StartRound();
			m_RoundCount++;
		} else {
			return;
		}
	}

	// Copied pause code from `gamecontroller`. I don't know why the code has
	// to be like this, but it works

	if (GameServer()->m_World.m_Paused && m_UnpauseTimer != 0)
	{
		--m_UnpauseTimer;
		if (m_UnpauseTimer == 0)
			GameServer()->m_World.m_Paused = false;
	}

	if (GameServer()->m_World.m_Paused)
	{
		++m_RoundStartTick;

		switch (m_GameState) {
			case IGS_WAITING_FLAG:
			case IGS_NORMAL:
			case IGS_FINISHING_OFF_ZOMBIES:
				++m_InfectionStartTick;
				break;
		}

		return;
	}

	if (m_GameStateTimer > 0)
		--m_GameStateTimer;

	CountPlayers();

	if (m_GameStateTimer == 0)
	{
		// Timer just fired
		switch (m_GameState)
		{
			case IGS_WAITING_FOR_INFECTION:
				DoInitialInfections();

				if (g_Config.m_SvZFNGExplain)
					ExplainWaitingNuke();

				m_InfectionStartTick = TICK;
				SetGameState(IGS_WAITING_FLAG);
				break;
			case IGS_WAITING_FLAG:
				SetGameState(IGS_NORMAL);
				break;
			case IGS_FINISHING_OFF_ZOMBIES:
				EndRound();
				break;
		}
	} else {
		// Timer is still running
		switch (m_GameState)
		{
			case IGS_WAITING_FOR_PLAYERS:
				{
					if (m_NumHumans + m_NumInfected >= m_Config.m_SvZFNGMinPlayers) {
						SetGameState(IGS_WAITING_FOR_INFECTION);
					} else {
						// Do broadcasts
						char aBuf[64];
						str_format(
							aBuf, sizeof aBuf,
							"Waiting for players"
						);
						m_Broadcaster.SetBroadcast(-1, aBuf, -1);
					}
					break;
				}
			case IGS_WAITING_FOR_INFECTION:
				{
					int Seconds =
						m_Config.m_SvZFNGInfectionDelay -
						((TICK - m_RoundStartTick) / TICK_SPEED);

					if (Seconds == 1) {
						m_Broadcaster.SetBroadcast(
							-1, "Starting infection in 1 second", TICK_SPEED * 1
						);
					} else {
						char aBuf[64];
						str_format(
							aBuf, sizeof aBuf,
							"%d 秒后开始感染", Seconds
						);
						m_Broadcaster.SetBroadcast(-1, aBuf, TICK_SPEED * 1);
					}
					break;
				}
			case IGS_WAITING_FLAG:
				{
					// Broadcast time for nuke to appear

					int Seconds =
						m_Config.m_SvZFNGNukeDelay -
						((TICK - m_InfectionStartTick) / TICK_SPEED);

					int Minutes = Seconds / 60;

					if (Minutes > 0 && Seconds % 60 == 0) {
						switch (Minutes) {
							case 10:
							case 5:
							case 4:
							case 3:
							case 2:
								char aBuf[64];
								str_format(
									aBuf, sizeof aBuf,
									"%d 分钟后出现核弹", Minutes
								);
								m_Broadcaster.SetBroadcast(
									-1, aBuf, TICK_SPEED * 1
								);
								break;
							case 1:
								m_Broadcaster.SetBroadcast(
									-1, "1分钟后出现核弹", TICK_SPEED * 1
								);
								break;
						}
					} else if (Minutes > 0 && Seconds % 60 == 30) {
						switch (Minutes) {
							case 10:
							case 5:
							case 4:
							case 3:
							case 2:
								char aBuf[64];
								str_format(
									aBuf, sizeof aBuf,
									"%d 秒/分钟后出现核弹, 30 秒", Minutes
								);
								m_Broadcaster.SetBroadcast(
									-1, aBuf, TICK_SPEED * 1
								);
								break;
							case 1:
								m_Broadcaster.SetBroadcast(
									-1, "核弹出现在 1 分钟后, 30秒", TICK_SPEED * 1
								);
								break;
						}
					} else {
						switch (Seconds) {
							case 30:
							case 10:
							case 5:
							case 4:
							case 3:
							case 2:
								char aBuf[64];
								str_format(
									aBuf, sizeof aBuf,
									"%d 秒后出现核弹", Seconds
								);
								m_Broadcaster.SetBroadcast(
									-1, aBuf, TICK_SPEED * 1
								);
								break;
							case 1:
								m_Broadcaster.SetBroadcast(
									-1,
									"Nuke spawns in 1 second",
									TICK_SPEED * 1
								);
								break;
						}
					}

					break;
				}
			case IGS_NORMAL:
				if (m_pFlag != NULL)
					DoFlag();
				break;
			case IGS_NUKE_DETONATED:
				if (m_Nuke->Update()) {
					RemoveNuke();
					SetGameState(IGS_FINISHING_OFF_ZOMBIES);
				}
				break;
		}
	}

	m_Broadcaster.Update();
	DoInactivePlayers();

	switch (m_GameState) {
		case IGS_WAITING_FLAG:
		case IGS_NORMAL:
		case IGS_NUKE_DETONATED:
			DoWincheck();
			break;
	}
}

bool CGameControllerZFNG::HasFlagHitDeath()
{
	// Reset if flag hits death or leaves the game layer
	int FlagCollision = GameServer()->Collision()->GetCollisionAt(
		m_pFlag->m_Pos.x,
		m_pFlag->m_Pos.y
	);

	int Collision =
		FlagCollision &
		(
			CCollision::COLFLAG_DEATH |
			CCollision::COLFLAG_SPIKE_NORMAL |
			CCollision::COLFLAG_SPIKE_RED |
			CCollision::COLFLAG_SPIKE_BLUE |
			CCollision::COLFLAG_SPIKE_GOLD |
			CCollision::COLFLAG_SPIKE_GREEN |
			CCollision::COLFLAG_SPIKE_PURPLE
		);

	return Collision != 0 || m_pFlag->GameLayerClipped(m_pFlag->m_Pos);
}

void CGameControllerZFNG::DoFlag()
{
	if (HasFlagHitDeath())
	{
		GameServer()->Console()->Print(
			IConsole::OUTPUT_LEVEL_DEBUG, "game", "flag_return"
		);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
		m_pFlag->Reset();
		return;
	}

	CCharacter* pCarrier = m_pFlag->m_pCarryingCharacter;
	if (pCarrier == NULL)
	{
		// Nobody is currently carrying the flag

		CCharacter *apCloseCCharacters[MAX_CLIENTS];
		int Num = GameServer()->m_World.FindEntities(
			m_pFlag->m_Pos,
			CFlag::ms_PhysSize,
			(CEntity**)apCloseCCharacters,
			MAX_CLIENTS,
			CGameWorld::ENTTYPE_CHARACTER
		);
		for (int i = 0; i < Num; i++)
		{
			CCharacter* pCharacter = apCloseCCharacters[i];
			if (!pCharacter->IsAlive() ||
				pCharacter->GetPlayer()->GetTeam() == TEAM_SPECTATORS ||
				GameServer()->Collision()->IntersectLine(
					m_pFlag->m_Pos,
					pCharacter->m_Pos,
					NULL, NULL
				)
			) { continue; }

			if (pCharacter->GetPlayer()->IsInfected()) {
				if (!m_pFlag->m_AtStand)
					ReturnFlag(pCharacter);
			}
			else {
				TakeFlag(pCharacter);
				break;
			}
		}

		if (pCarrier == NULL && !m_pFlag->m_AtStand)
			DoDroppedFlag();
	} else {
		// Somebody is currently carrying the flag

		// Update flag position
		m_pFlag->m_Pos = pCarrier->m_Pos;

		float CaptureDistance = distance(
			m_pFlag->m_Pos,
			m_aFlagStandPositions[NukeDetonatorTeam()]
		);
		float MaxCaptureDistance = CFlag::ms_PhysSize + CCharacter::ms_PhysSize;
		if (CaptureDistance < MaxCaptureDistance) {
			// The flag was captured
			DoFlagCapture();
		}
	}
}

void CGameControllerZFNG::ReturnFlag(CCharacter* pCharacter)
{
	pCharacter->GetPlayer()->m_Score++;
	int cid = pCharacter->GetPlayer()->GetCID();

	char aBuf[256];
	str_format(
		aBuf, sizeof(aBuf),
		"flag_return player='%d:%s'",
		cid, Server()->ClientName(cid)
	);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	m_pFlag->Reset();
}

void CGameControllerZFNG::TakeFlag(CCharacter* pCharacter)
{
	if (m_pFlag->m_AtStand)
		m_pFlag->m_GrabTick = TICK;

	m_pFlag->m_AtStand = 0;
	m_pFlag->m_pCarryingCharacter = pCharacter;
	pCharacter->GetPlayer()->m_Score++;
	int cid = pCharacter->GetPlayer()->GetCID();

	char aBuf[256];
	str_format(
		aBuf, sizeof(aBuf),
		"flag_grab player='%d:%s'",
		cid,
		Server()->ClientName(cid)
	);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	for (int c = 0; c < MAX_CLIENTS; c++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[c];
		if (pPlayer == NULL)
			continue;

		if (pPlayer->GetTeam() == TEAM_SPECTATORS &&
			pPlayer->m_SpectatorID != SPEC_FREEVIEW &&
			GameServer()->m_apPlayers[pPlayer->m_SpectatorID] &&
			GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetTeam() == TEAM_INFECTED
		)
			GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_EN, c);
		else if (pPlayer->GetTeam() == TEAM_INFECTED)
			GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_EN, c);
		else
			GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_PL, c);
	}
}

void CGameControllerZFNG::DoDroppedFlag()
{
	CCharacter* pCarrier = m_pFlag->m_pCarryingCharacter;
	if (pCarrier == NULL && !m_pFlag->m_AtStand)
	{
		if (TICK > m_pFlag->m_DropTick + TICK_SPEED * 30)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
			m_pFlag->Reset();
		}
		else
		{
			m_pFlag->m_Vel.y += GameServer()->m_World.m_Core.m_Tuning.m_Gravity;
			GameServer()->Collision()->MoveBox(
				&m_pFlag->m_Pos,
				&m_pFlag->m_Vel,
				vec2(m_pFlag->ms_PhysSize, m_pFlag->ms_PhysSize),
				0.5f
			);
		}
	}
}

void CGameControllerZFNG::DoFlagCapture()
{
	CCharacter* pCarrier = m_pFlag->m_pCarryingCharacter;
	int carrierCid = pCarrier->GetPlayer()->GetCID();
	pCarrier->GetPlayer()->m_Score += 5;

	char aBuf[512];
	str_format(
		aBuf, sizeof(aBuf),
		"flag_capture player='%d:%s'",
		carrierCid,
		Server()->ClientName(carrierCid)
	);
	GameServer()->Console()->Print(
		IConsole::OUTPUT_LEVEL_DEBUG,
		"game",
		aBuf
	);

	float CaptureTime =
		(TICK - m_pFlag->m_GrabTick) / (float)(TICK_SPEED);

	str_format(
		aBuf, sizeof(aBuf),
		"'%s' 引爆了核弹 (%d.%s%d 秒)",
		Server()->ClientName(carrierCid),
		(int)CaptureTime % 60,
		((int)(CaptureTime * 100) % 100) < 10 ? "0" : "",
		(int)(CaptureTime * 100) % 100
	);
	GameServer()->SendChat(-1, -2, aBuf);
	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
	RemoveFlag();
	SetGameState(IGS_NUKE_DETONATED);
}

void CGameControllerZFNG::DoInactivePlayers()
{

	if(m_Config.m_SvInactiveKickTime > 0 && !m_Config.m_SvTournamentMode)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !Server()->IsAuthed(i))
			{
				if(TICK > GameServer()->m_apPlayers[i]->m_LastActionTick+m_Config.m_SvInactiveKickTime*TICK_SPEED*60)
				{
					switch(m_Config.m_SvInactiveKick)
					{
					case 0:
						{
							// move player to spectator
							((CPlayer*)GameServer()->m_apPlayers[i])->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 1:
						{
							// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
							int Spectators = 0;
							for(int j = 0; j < MAX_CLIENTS; ++j)
								if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
									++Spectators;
							if(Spectators >= m_Config.m_SvSpectatorSlots)
								Server()->Kick(i, "Kicked for inactivity");
							else
								((CPlayer*)GameServer()->m_apPlayers[i])->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 2:
						{
							// kick the player
							Server()->Kick(i, "Kicked for inactivity");
						}
					}
				}
			}
		}
	}
}

void CGameControllerZFNG::DoWincheck()
{
	if (m_GameOverTick == -1 &&
		!GameServer()->m_World.m_ResetRequested
	) {
		if (m_NumHumans == 0) {
			// Zombies win
			dbg_msg("zfng", "僵尸胜利!");
			EndRound();
			return;
		} else if (m_NumInfected == 0) {
			// Humans win
			dbg_msg("zfng", "人类胜利!");
			SetGameState(IGS_FINISHING_OFF_ZOMBIES);
			return;
		}

		bool TimeLimitMet =
			m_Config.m_SvTimelimit > 0 &&
			(TICK - m_RoundStartTick) >= m_Config.m_SvTimelimit * TICK_SPEED * 60;

		if (TimeLimitMet) {
			EndRound();
		}
	}
}

void CGameControllerZFNG::Snap(int SnappingClient)
{
	IGameController::Snap(SnappingClient);

	CNetObj_GameData *pGameDataObj =
		(CNetObj_GameData *)Server()->SnapNewItem(
			NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData)
		);

	if (pGameDataObj == NULL)
		return;

	if (m_pFlag == NULL) {
		pGameDataObj->m_FlagCarrierBlue = FLAG_MISSING;
	} else {
		CCharacter* pCarrier = m_pFlag->m_pCarryingCharacter;

		if (m_pFlag->m_AtStand) {
			pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
		} else if (pCarrier && pCarrier->GetPlayer()) {
			pGameDataObj->m_FlagCarrierBlue = pCarrier->GetPlayer()->GetCID();
		} else {
			pGameDataObj->m_FlagCarrierBlue = FLAG_TAKEN;
		}
	}

	pGameDataObj->m_FlagCarrierRed = FLAG_MISSING;
}

int CGameControllerZFNG::NukeStandTeam()
{
	return g_Config.m_SvZFNGSwapFlags ? TEAM_RED : TEAM_BLUE;
}

int CGameControllerZFNG::NukeDetonatorTeam()
{
	return g_Config.m_SvZFNGSwapFlags ? TEAM_BLUE : TEAM_RED;
}

bool CGameControllerZFNG::OnEntity(int Index, vec2 Pos)
{
	if (IGameController::OnEntity(Index, Pos))
		return true;

	switch (Index) {
		case ENTITY_FLAGSTAND_RED:
			SpawnFlagStand(TEAM_RED, Pos);
			return true;
		case ENTITY_FLAGSTAND_BLUE:
			SpawnFlagStand(TEAM_BLUE, Pos);
			return true;
	}

	return false;
}

void CGameControllerZFNG::OnCharacterSpawn(class CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(10);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_RIFLE, -1);
}

int CGameControllerZFNG::OnCharacterDeath(
	class CCharacter *pVictim,
	class CPlayer *pKiller,
	int Weapon
) {
	// Scoring
	if (!pKiller || Weapon == WEAPON_GAME) {
		return DropFlagMaybe(pVictim, pKiller);
	}

	if (pKiller == pVictim->GetPlayer()) {
		pVictim->GetPlayer()->m_Score--; // Suicide
	} else {
		if (Weapon == WEAPON_RIFLE || Weapon == WEAPON_GRENADE){
			// Ignore teamkills
			pKiller->m_Score++; // Normal kill
			if (m_pFlag && m_pFlag->m_pCarryingCharacter == pVictim)
			{
				GameServer()->CreateSoundGlobal(SOUND_PLAYER_PAIN_LONG);
				pKiller->m_Score++; // Froze flag carrier
			}
		} else if (Weapon == WEAPON_SPIKE_NORMAL) {
			pVictim->GetPlayer()->m_RespawnTick = TICK + TICK_SPEED * 0.5f;
			pKiller->m_Score += m_Config.m_SvPlayerScoreSpikeNormal;
			if (pKiller->GetCharacter())
				GameServer()->MakeLaserTextPoints(
					pKiller->GetCharacter()->m_Pos,
					pKiller->GetCID(),
					m_Config.m_SvPlayerScoreSpikeNormal
				);
		} else if (Weapon == WEAPON_SPIKE_RED || Weapon == WEAPON_SPIKE_BLUE) {
			pVictim->GetPlayer()->m_RespawnTick = TICK + TICK_SPEED * 0.5f;
			pKiller->m_Score += m_Config.m_SvPlayerScoreSpikeTeam;
			if (pKiller->GetCharacter())
				GameServer()->MakeLaserTextPoints(
					pKiller->GetCharacter()->m_Pos,
					pKiller->GetCID(),
					m_Config.m_SvPlayerScoreSpikeTeam
				);
		} else if (Weapon == WEAPON_SPIKE_GOLD) {
			pVictim->GetPlayer()->m_RespawnTick = TICK + TICK_SPEED * 0.5f;
			pKiller->m_Score += m_Config.m_SvPlayerScoreSpikeGold;
			if (pKiller->GetCharacter())
				GameServer()->MakeLaserTextPoints(
					pKiller->GetCharacter()->m_Pos,
					pKiller->GetCID(),
					m_Config.m_SvPlayerScoreSpikeGold
				);
		} else if (Weapon == WEAPON_HAMMER) {
			// Only called if teammate unfroze you
			pKiller->m_Score++;
		}
	}

	if (Weapon == WEAPON_SELF) {
		pVictim->GetPlayer()->m_RespawnTick = TICK + TICK_SPEED * 0.75f;
	} else if (Weapon == WEAPON_WORLD) {
		pVictim->GetPlayer()->m_RespawnTick = TICK + TICK_SPEED * 0.75f;
	}

	if (Weapon != WEAPON_RIFLE &&
		Weapon != WEAPON_GRENADE &&
		Weapon != WEAPON_HAMMER
	) {
		// Only drop the flag if the victim is killed, not frozen
		return DropFlagMaybe(pVictim, pKiller);
	} else {
		return 0;
	}
}

int CGameControllerZFNG::DropFlagMaybe(
	class CCharacter* pVictim,
	class CPlayer* pKiller
) {
	int HadFlag = 0;
	for (int i = 0; i < 2; i++)
	{
		if (m_pFlag &&
			pKiller &&
			pKiller->GetCharacter() &&
			m_pFlag->m_pCarryingCharacter == pKiller->GetCharacter()
		) { HadFlag |= 2; }

		if (m_pFlag && m_pFlag->m_pCarryingCharacter == pVictim)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_DROP);
			m_pFlag->m_DropTick = TICK;
			m_pFlag->m_pCarryingCharacter = 0;
			m_pFlag->m_Vel = vec2(0,0);

			// Don't reward points for sacrificing the flagger, because we
			// already award points for freezing him/her

			HadFlag |= 1;
		}
	}

	return HadFlag;
}

void CGameControllerZFNG::PostReset() {
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->Respawn();
			GameServer()->m_apPlayers[i]->m_Score = 0;
			GameServer()->m_apPlayers[i]->m_ScoreStartTick = TICK;
			GameServer()->m_apPlayers[i]->m_RespawnTick = TICK + TICK_SPEED / 2;
		}
	}
}

void CGameControllerZFNG::StartRound()
{
	IGameController::StartRound();
	UninfectAll();
	RemoveFlag();
	RemoveNuke();
	SetGameState(IGS_WAITING_FOR_PLAYERS);
}

void CGameControllerZFNG::EndRound()
{
	IGameController::EndRound();
	SetGameState(IGS_ROUND_ENDED);
	AnnounceWinners();
}

void CGameControllerZFNG::AnnounceWinners()
{
	char aBuf[64];
	if (m_NumHumans == 0) {
		str_format(aBuf, sizeof(aBuf), "没有人类幸存");
	} else if (m_NumHumans == 1) {
		str_format(aBuf, sizeof(aBuf), "一个人类幸存");
	} else {
		str_format(
			aBuf, sizeof(aBuf),
			"%d个人类幸存", m_NumHumans
		);
	}
	GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
}

bool CGameControllerZFNG::IsWaitingForPlayers()
{
	switch (m_GameState) {
		case IGS_WAITING_FOR_PLAYERS:
			return true;
		default:
			return false;
	}
}

bool CGameControllerZFNG::IsInfectionStarted()
{
	switch (m_GameState) {
		case IGS_WAITING_FOR_PLAYERS:
		case IGS_WAITING_FOR_INFECTION:
			return false;
		default:
			return true;
	}
}

int CGameControllerZFNG::GetAutoTeam(int NotThisID)
{
	if (Server()->WasClientSpectating(NotThisID)) {
		return TEAM_SPECTATORS;
	}

	int result;
	if (IsInfectionStarted()) {
		result = TEAM_INFECTED;
	} else {
		result = TEAM_HUMAN;
	}

	if (CanJoinTeam(result, NotThisID))
		return result;
	else
		return -1;
}

bool CGameControllerZFNG::CanChangeTeam(CPlayer *pPlayer, int JoinTeam)
{
	if (IsInfectionStarted()) {
		if (JoinTeam == TEAM_INFECTED || JoinTeam == TEAM_SPECTATORS) {
			return true;
		} else {
			return false;
		}
	} else {
		return true;
	}
}

bool CGameControllerZFNG::CheckTeamBalance()
{
	return true;
}

bool CGameControllerZFNG::CanSpawn()
{
	switch (m_GameState) {
		case IGS_NUKE_DETONATED:
		case IGS_FINISHING_OFF_ZOMBIES:
		case IGS_ROUND_ENDED:
			return false;
		default:
			return true;
	}
}

bool CGameControllerZFNG::CanSpawn(int Team, vec2* pOutPos)
{
	return CanSpawn() && IGameController::CanSpawn(Team, pOutPos);
}

void CGameControllerZFNG::CountPlayers() {
	m_NumHumans = 0;
	m_NumInfected = 0;

	// Loop through and increment them
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		// Check that the player is able to play (not spectating)
		if (GameServer()->IsClientPlayer(i)) {
			if (GameServer()->m_apPlayers[i]->IsInfected())
				m_NumInfected++;
			else
				m_NumHumans++;
		}
	}

}

int CGameControllerZFNG::CalcNumInitialInfections() {
	int Total = m_NumHumans + m_NumInfected;
	switch (g_Config.m_SvZFNGInitialInfections) {
		case 0:
			return (int)(sqrt(Total * 0.5f));
		case 1:
			return (int)(sqrt(Total));
		case 2:
			return min((int)(sqrt(Total * 2)), Total - 1);
	}
}

void CGameControllerZFNG::SpawnFlag()
{
	int Team = NukeStandTeam();
	if (m_aFlagStandPositions[Team] != NULL) {
		m_pFlag = new CFlag(&GameServer()->m_World, TEAM_INFECTED);
		vec2 StandPos = m_aFlagStandPositions[Team];
		m_pFlag->m_StandPos = StandPos;
		m_pFlag->m_Pos = StandPos;
		GameServer()->m_World.InsertEntity(m_pFlag);
	}
}

void CGameControllerZFNG::AnnounceNuke()
{
	if (NukeStandTeam() == TEAM_INFECTED) {
		m_Broadcaster.SetBroadcast(
			-1, "The nuke has spawned in the zombie base", TICK_SPEED * 3
		);
	} else {
		m_Broadcaster.SetBroadcast(
			-1, "The nuke has spawned in the human base", TICK_SPEED * 3
		);
	}

	if (g_Config.m_SvZFNGExplain)
		ExplainNukeSpawned();

	GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
}

void CGameControllerZFNG::ExplainWaitingNuke()
{
	if (g_Config.m_SvZFNGNukeDelay <= 10)
		return;

	if (g_Config.m_SvZFNGSwapFlags) {
		GameServer()->SendChat(
			-1,
			NukeDetonatorTeam(), // TEAM_INFECTED
			"• 在核武器出现前感染所有人类!"
		);
		GameServer()->SendChat(
			-1,
			NukeStandTeam(), // TEAM_HUMAN
			"• 在核武器出现前守住你的阵地!"
		);
	} else {
		GameServer()->SendChat(
			-1,
			NukeDetonatorTeam(), // TEAM_HUMAN
			"• 在核武器出现前幸存下来!"
		);
		GameServer()->SendChat(
			-1,
			NukeStandTeam(), // TEAM_INFECTED
			"• 在核武器出现前守住你的阵地!"
		);
	}
}

void CGameControllerZFNG::ExplainNukeSpawned()
{
	if (g_Config.m_SvZFNGSwapFlags) {
		GameServer()->SendChat(
			-1,
			NukeDetonatorTeam(), // TEAM_INFECTED
			"• 不要让人类在你的阵地上安置核武器!"
		);
		GameServer()->SendChat(
			-1,
			NukeStandTeam(), // TEAM_HUMAN
			"• 在僵尸阵地上安置核武器以消灭他们!"
		);
	} else {
		GameServer()->SendChat(
			-1,
			NukeDetonatorTeam(), // TEAM_HUMAN
			"• 从僵尸处获得核武器以胜利!"
		);
		GameServer()->SendChat(
			-1,
			NukeStandTeam(), // TEAM_INFECTED
			"• 不要让核武器落到人类手中!"
		);
	}
}

void CGameControllerZFNG::RemoveFlag()
{
	if (m_pFlag != NULL) {
		GameServer()->m_World.DestroyEntity(m_pFlag);
		m_pFlag = NULL;
	}
}

void CGameControllerZFNG::RemoveNuke()
{
	if (m_Nuke != NULL) {
		delete m_Nuke;
		m_Nuke = NULL;
	}
}

void CGameControllerZFNG::SpawnFlagStand(int Team, vec2 Pos)
{
	m_aFlagStandPositions[Team] = Pos;

	int FlagStandType = (Team == NukeStandTeam()) ?
		CFlagStand::NUKE_STAND : CFlagStand::NUKE_DETONATOR;

	m_apFlagStands[Team] = new CFlagStand(&GameServer()->m_World, FlagStandType);

	m_apFlagStands[Team]->m_Pos = Pos;
	GameServer()->m_World.InsertEntity(m_apFlagStands[Team]);
}

void CGameControllerZFNG::FinishOffZombies()
{
	CCharacter* p = (CCharacter*)GameServer()->m_World
		.FindFirst(CGameWorld::ENTTYPE_CHARACTER);

	CCharacter* pNext;

	while (p != NULL) {
		// We have to do this before `Die` because `Die` will remove it from
		// the linked list
		pNext = (CCharacter *)p->TypeNext();

		if (p->GetPlayer()->IsInfected()) {
			GameServer()->CreateExplosion(p->m_Pos, -1, WEAPON_GAME, true);
			GameServer()->CreateSound(p->m_Pos, SOUND_GRENADE_EXPLODE);
			p->Die(p->GetPlayer()->GetCID(), WEAPON_GAME);
		}

		p = pNext;
	}
}

void CGameControllerZFNG::DoInitialInfections() {
	int NumMinimumInfected = CalcNumInitialInfections();
	if (m_NumInfected < NumMinimumInfected) {
		int NumToInfect = NumMinimumInfected - m_NumInfected;
		while (NumToInfect > 0)
		{
			// Guilty until proven innocent
			bool AllClientsHaveBeenInfectedBefore = true;

			// Loop through all and infect if not infected before
			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				if (GameServer()->IsClientPlayer(i) &&
					!Server()->WasClientInfectedBefore(i)
				) {
					// Technically this may not be accurate but having just any
					// number of previously uninfected clients will cause the
					// suspicion that `AllClientsHaveBeenInfectededBefore` is
					// false because this loop is itself the method to check if
					// `AllClientsHaveBeenInfectedBefore`
					AllClientsHaveBeenInfectedBefore = false;

					GameServer()->m_apPlayers[i]->Infect(true, false);

					// Remember infection because it was picked by the server
					// (we remember their bad luck)
					Server()->RememberInfection(i);

					// Update counters
					NumToInfect--;
					m_NumHumans--;
					m_NumInfected++;

					// Check if quota is met
					if (NumToInfect == 0)
						break;
				}
			}

			if (AllClientsHaveBeenInfectedBefore)
			{
				Server()->ForgetAllInfections();
				// Now the next iteration of the 'while' loop should finish the
				// job
			}
		}
	}
}

void CGameControllerZFNG::UninfectAll()
{
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		CPlayer* player = GameServer()->m_apPlayers[i];
		if (player != NULL) {
			player->m_HumanTime = 0;
			if (player->IsInfected()) {
				player->Revive(false, false);
			}
		}
	}
}
