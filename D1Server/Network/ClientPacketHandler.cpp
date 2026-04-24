#include "Network/ClientPacketHandler.h"

#include "DB/DBConnection.h"
#include "DB/DBContext.h"
#include "DB/DBJobQueue.h"
#include "Network/GameServerSession.h"
#include "World/Account.h"
#include "World/Level.h"
#include "World/PlayerEntry.h"
#include "World/World.h"

#include <cstring>
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
		Entry.CharacterType = static_cast<Protocol::CharacterType>(NewPlayerID % 3);
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

		// DBSet<T>::Find 는 T* (nullptr = 행 없음) 반환. Identified 맵이 소유, 여기서는 non-owning ptr.
		Account* AccPtr = Accounts.Find(Id);

		PlayerEntry Entry;
		if (AccPtr != nullptr)
		{
			// 기존 계정 — 비밀번호 검증 후 PlayerEntry 로드. 쓰기 없음 → SaveChanges 불필요.
			if (std::strcmp(AccPtr->Password, Pw.c_str()) != 0)
			{
				SendLoginResult(GameSession, Protocol::LR_INVALID_CREDENTIALS);
				return;
			}
			PlayerEntry* EntryPtr = Players.Find(static_cast<uint64>(AccPtr->PlayerID));
			if (EntryPtr == nullptr)
			{
				SendLoginResult(GameSession, Protocol::LR_DB_ERROR);
				return;
			}
			// Identified entry 는 람다 스코프에서 소멸 — 이후 세션 경로에 전달하려면 값 복사 1회.
			Entry = *EntryPtr;
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
			NewEntry->CharacterType = static_cast<Protocol::CharacterType>(NewPlayerID % 3);
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

	World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoTryMove, PlayerID, pkt.dir(), pkt.client_seq());
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