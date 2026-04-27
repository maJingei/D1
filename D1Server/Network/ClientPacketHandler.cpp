#include "Network/ClientPacketHandler.h"

#include "DB/DBConnection.h"
#include "DB/DBContext.h"
#include "DB/DBJobQueue.h"
#include "Network/GameServerSession.h"
#include "World/Account.h"
#include "World/Level.h"
#include "World/PlayerEntry.h"
#include "World/World.h"

#include <iostream>
#include <string>

PacketHandlerFunc GPacketHandler[UINT16_MAX];

namespace
{
	/** S_LOGIN 실패 회신을 1줄로 보내는 헬퍼. */
	void SendLoginResult(const PacketSessionRef& Session, Protocol::ELoginResult Result, uint64 UserId = 0)
	{
		Protocol::S_LOGIN Res;
		Res.set_result(static_cast<uint32>(Result));
		Res.set_user_id(UserId);
		Session->Send(ClientPacketHandler::MakeSendBuffer(Res));
	}
}

const char* GetPacketName(uint16 PacketId)
{
	switch (PacketId)
	{
		case PKT_C_LOGIN: return "C_LOGIN";
		case PKT_S_LOGIN: return "S_LOGIN";
		case PKT_C_ENTER_GAME: return "C_ENTER_GAME";
		case PKT_S_ENTER_GAME: return "S_ENTER_GAME";
		case PKT_S_SPAWN: return "S_SPAWN";
		case PKT_C_MOVE: return "C_MOVE";
		case PKT_S_MOVE: return "S_MOVE";
		case PKT_S_MOVE_REJECT: return "S_MOVE_REJECT";
		case PKT_S_MONSTER_SPAWN: return "S_MONSTER_SPAWN";
		case PKT_S_MONSTER_MOVE: return "S_MONSTER_MOVE";
		case PKT_S_MONSTER_ATTACK: return "S_MONSTER_ATTACK";
		case PKT_S_PLAYER_DAMAGED: return "S_PLAYER_DAMAGED";
		case PKT_S_PLAYER_DIED: return "S_PLAYER_DIED";
		case PKT_C_ATTACK: return "C_ATTACK";
		case PKT_S_MONSTER_DAMAGED: return "S_MONSTER_DAMAGED";
		case PKT_S_MONSTER_DIED: return "S_MONSTER_DIED";
		case PKT_S_PORTAL_TELEPORT: return "S_PORTAL_TELEPORT";
		case PKT_S_PLAYER_LEFT: return "S_PLAYER_LEFT";
		case PKT_S_PLAYER_ATTACK: return "S_PLAYER_ATTACK";
		case PKT_C_CHAT: return "C_CHAT";
		case PKT_S_CHAT: return "S_CHAT";
		default: return nullptr;
	}
}

bool Handle_INVALID(PacketSessionRef& /*session*/, BYTE* buffer, int32 /*len*/)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	std::cout << "[Server] Invalid packet id=" << header->Id << "\n";
	return false;
}

bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);

	// State guard — 이미 Playing 이면 C_LOGIN 재전송을 거절(패킷 flood + 상태 보호).
	if (GameSession->GetState() != ESessionState::NotLoggedIn)
	{
		std::cout << "[Server] C_LOGIN rejected (already playing)\n";
		return false;
	}

	// LoadBot 전용 경로 — DB/중복 체크 skip, 메모리 전용 세션으로 즉시 진입.
	// AccountId 를 비워 두면 OnDisconnected 에서 save 경로로 분기되지 않는다.
	if (pkt.is_bot())
	{
		const uint64 NewPlayerID = World::GetInstance().AllocNewPlayerID();

		PlayerEntry Entry;
		Entry.PlayerID = NewPlayerID;
		// 봇은 식별을 쉽게 하기 위해 항상 사무라이 스프라이트로 고정.
		Entry.CharacterType = Protocol::CT_SAMURAI;
		Entry.LevelID = static_cast<int32>(NewPlayerID % static_cast<uint64>(LEVEL_COUNT));
		const auto& Walkables = World::GetInstance().GetLevel(Entry.LevelID)->WalkableTiles;
		if (Walkables.empty() == false)
		{
			const auto& Tile = Walkables[NewPlayerID % Walkables.size()];
			Entry.TileX = Tile.first;
			Entry.TileY = Tile.second;
		}
		Entry.HP = 20;
		Entry.MaxHP = 20;
		Entry.AttackDamage = 5;
		Entry.TileMoveSpeed = 6.0f;

		// 봇 nameplate = 서버 전역 카운터 숫자. 사람과 시각적으로 구분되는 짧은 ID.
		Entry.NameplateText = std::to_string(World::GetInstance().AllocNewBotId());

		GameSession->SetState(ESessionState::Playing);
		SendLoginResult(GameSession, Protocol::LR_SUCCESS, NewPlayerID);
		World::GetInstance().EnterFromLogin(GameSession, std::move(Entry));
		return true;
	}

	// 입력 포맷 검증 — 빈 문자열 금지, VARCHAR(32) 초과 금지.
	const std::string Id = pkt.id();
	const std::string Pw = pkt.pw();
	if (Id.empty() || Pw.empty() || Id.size() > 32 || Pw.size() > 32)
	{
		SendLoginResult(session, Protocol::LR_INVALID_REQUEST_FORMAT);
		return true;
	}

	DBJobQueue::GetInstance().Schedule([GameSession, Id, Pw](DBConnection& Conn)
	{
		DBContext Ctx(Conn);
		auto& Accounts = Ctx.Set<Account>();
		auto& Players = Ctx.Set<PlayerEntry>();

		Account* AccPtr = Accounts
						.Where(AccountCol::Id == Id.c_str())
						.SingleOrDefault();
		
		PlayerEntry Entry;
		if (AccPtr != nullptr)
		{
			// 기존 계정 — 비밀번호 검증 후 PlayerEntry 로드. 쓰기 없음 → SaveChanges 불필요.
			if (std::strcmp(AccPtr->Password, Pw.c_str()) != 0)
			{
				SendLoginResult(GameSession, Protocol::LR_INVALID_CREDENTIALS);
				return;
			}
			// PlayerEntry* EntryPtr = Players.Find(static_cast<uint64>(AccPtr->PlayerID));
			PlayerEntry* EntryPtr = Players.Where(PlayerEntryCol::PlayerID == (AccPtr->PlayerID)).SingleOrDefault();
			if (EntryPtr == nullptr)
			{
				SendLoginResult(GameSession, Protocol::LR_DB_ERROR);
				return;
			}
			// Identified entry 는 람다 스코프에서 소멸 — 이후 세션 경로에 전달하려면 값 복사 1회.
			Entry = *EntryPtr;
			// 사람 nameplate = 로그인 시 입력한 Account.Id 그대로. NameplateText 는 DB 미매핑이라 직접 채워준다.
			Entry.NameplateText = Id;
			// 사람 클라는 봇(CT_SAMURAI)과 시각적으로 구분되도록 신규 4방향 PlayerSprite(CT_DEFAULT) 사용. (DB 값 무시)
			Entry.CharacterType = Protocol::CT_DEFAULT;
		}
		else
		{
			// 자동 가입 — Account + PlayerEntry 를 한 트랜잭션으로 묶어 원자적으로 INSERT.
			const uint64 NewPlayerID = World::GetInstance().AllocNewPlayerID();

			// TODO : strncpy_s 이거는 나중에 고쳐보자
			auto NewAcc = std::make_shared<Account>();
			strncpy_s(NewAcc->Id, Id.c_str(), Id.size());
			strncpy_s(NewAcc->Password, Pw.c_str(), Pw.size());
			NewAcc->PlayerID = static_cast<int64>(NewPlayerID);
			Accounts.Add(NewAcc);

			// 신규 PlayerEntry 기본 스폰 구성 — WalkableTiles 에서 결정론적 위치 선택.
			auto NewEntry = std::make_shared<PlayerEntry>();
			NewEntry->PlayerID = NewPlayerID;
			// 사람 클라는 봇(CT_SAMURAI)과 시각적으로 구분되도록 신규 4방향 PlayerSprite(CT_DEFAULT) 사용.
			NewEntry->CharacterType = Protocol::CT_DEFAULT;
			NewEntry->LevelID = static_cast<int32>(NewPlayerID % static_cast<uint64>(LEVEL_COUNT));
			const auto& Walkables = World::GetInstance().GetLevel(NewEntry->LevelID)->WalkableTiles;
			if (Walkables.empty() == false)
			{
				const auto& Tile = Walkables[NewPlayerID % Walkables.size()];
				NewEntry->TileX = Tile.first;
				NewEntry->TileY = Tile.second;
			}
			NewEntry->HP = 20;
			NewEntry->MaxHP = 20;
			NewEntry->AttackDamage = 5;
			NewEntry->TileMoveSpeed = 6.0f;
			Players.Add(NewEntry);

			// 원자적 커밋 — 둘 중 하나라도 실패하면 ROLLBACK, DB 불일치 없음.
			if (Ctx.SaveChanges() == false)
			{
				SendLoginResult(GameSession, Protocol::LR_DB_ERROR);
				return;
			}

			// 세션 경로로 전달할 값 복사(Schedule 람다 종료 시 NewEntry 는 함께 소멸).
			Entry = *NewEntry;
			// 신규 가입 사람도 nameplate = 입력한 Account.Id.
			Entry.NameplateText = Id;
		}

		// 중복 로그인 체크 — World 맵에 이미 동일 AccountId 활성 세션이 있으면 거절.
		if (World::GetInstance().TryRegisterAccount(Id, GameSession) == false)
		{
			SendLoginResult(GameSession, Protocol::LR_ALREADY_LOGGED_IN);
			return;
		}

		// 세션 상태 업데이트 — DB 쪽 확정이 끝난 후 IOCP/다른 워커가 읽도록 atomic 저장.
		GameSession->SetAccountId(Id);
		GameSession->SetState(ESessionState::Playing);

		// S_LOGIN 성공 회신.
		SendLoginResult(GameSession, Protocol::LR_SUCCESS, Entry.PlayerID);

		// 월드 진입 — Level JobQueue 로 넘겨서 S_ENTER_GAME 응답 + S_SPAWN 브로드캐스트 수행.
		World::GetInstance().EnterFromLogin(GameSession, std::move(Entry));
	});

	return true;
}

bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt)
{
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	const uint64 PlayerID = GameSession->GetPlayerID();
	if (PlayerID == 0)
		return false;

	const int32 LevelID = GameSession->GetLevelID();
	if (LevelID < 0)
		return true;

	World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoTryMove, PlayerID, pkt.dir(), pkt.client_seq(), pkt.client_delta_ms());
	return true;
}

bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& /*pkt*/)
{
	// M4.5 에서 로그인 경로가 진입을 대신하므로 본 핸들러는 레거시. 실제 사용처 없음.
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	World::GetInstance().EnterAnyLevel(GameSession);
	return true;
}

bool Handle_C_ATTACK(PacketSessionRef& session, Protocol::C_ATTACK& /*pkt*/)
{
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	const uint64 PlayerID = GameSession->GetPlayerID();
	if (PlayerID == 0)
		return false;

	const int32 LevelID = GameSession->GetLevelID();
	if (LevelID < 0)
		return true;

	World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoTryAttack, PlayerID);
	return true;
}

bool Handle_C_DEBUG_FORCE_REJECT(PacketSessionRef& session, Protocol::C_DEBUG_FORCE_REJECT& pkt)
{
#ifdef _DEBUG
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	const uint64 PlayerID = GameSession->GetPlayerID();
	if (PlayerID == 0)
	{
		return false;
	}

	const int32 LevelID = GameSession->GetLevelID();
	if (LevelID < 0)
	{
		return true;
	}

	// 카운터 세팅은 PlayerEntry 를 만지므로 Level JobQueue 직렬화 안에서 수행해야 한다 — 동일 프레임에 도착하는 후속 C_MOVE 와 동일한 Job 큐를 공유.
	World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoSetDebugForceReject, PlayerID, pkt.reject_at_nth_packet(), pkt.cooldown_bypass_count());
#else
	(void)session;
	(void)pkt;
#endif
	return true;
}

bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	const uint64 PlayerID = GameSession->GetPlayerID();
	if (PlayerID == 0)
		return false;

	const int32 LevelID = GameSession->GetLevelID();
	if (LevelID < 0)
		return true;

	// 메시지 길이 cap — UTF-8 byte 단위. 한글이면 약 85자 정도, ASCII 면 256자.
	// 빈 메시지는 broadcast 하지 않는다(클라 ESC/빈 엔터로 닫힌 케이스 방어).
	static constexpr size_t MaxChatLengthBytes = 256;
	std::string Text = pkt.text();
	if (Text.empty())
		return true;
	if (Text.size() > MaxChatLengthBytes)
		Text.resize(MaxChatLengthBytes);

	World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoBroadcastChat, PlayerID, std::move(Text));
	return true;
}