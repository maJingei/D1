-- dbo.PlayerEntry — M2 단계 수동 CRUD 타겟 테이블.
-- PlayerID(BIGINT) 를 PK 로 하는 단일 행 플레이어 영속 상태.
-- 컬럼 정의 근거: .omc/specs/deep-interview-m2-player-crud.md #Constraints (영속 9컬럼, LastDir 제외).
--
-- 재생성 정책: 학습 프로젝트라 데이터 손실을 허용하고 DROP → CREATE 순서로 편하게 실행한다.
-- M3 에서 매크로 기반 메타데이터가 도입되면 이 파일은 '정답지' 역할만 하고 런타임이 스스로 CREATE 하는
-- 방향으로 전환될 수 있다. 현 단계에서는 SSMS 에서 수동 실행.

IF OBJECT_ID('dbo.PlayerEntry', 'U') IS NOT NULL
    DROP TABLE dbo.PlayerEntry;

CREATE TABLE dbo.PlayerEntry
(
    -- 플레이어 고유 식별자. 서버 메모리 NextPlayerID 와 1:1 매핑하며 재접속 시 같은 행을 Find 로 복원.
    PlayerID BIGINT NOT NULL PRIMARY KEY,

    -- Protocol::CharacterType (enum) 을 INT 로 저장. DoEnter 에서 PlayerID 해시로 자동 배정된 코스메틱 타입.
    CharacterType INT NOT NULL,

    -- PlayerEntry 구조체에는 없지만 M2 에서 신규 추가되는 필드. GameServerSession::LevelID 와 동일 값을 보관.
    LevelID INT NOT NULL,

    -- 타일 좌표 (게임 로직 단위). 픽셀 좌표는 클라 렌더링 계산 산출물이므로 서버는 저장하지 않는다.
    TileX INT NOT NULL,
    TileY INT NOT NULL,

    -- 현재 HP / 최대 HP. 전투 중 변동은 메모리에서만, Logout=Save 때 한 번만 영속화 (M2.5+ 에 붙일 예정).
    HP INT NOT NULL,
    MaxHP INT NOT NULL,

    -- 1회 공격 데미지. 공격자 책임 원칙에 따라 이 값이 곧바로 Monster.ApplyDamage 로 전달됨.
    AttackDamage INT NOT NULL,

    -- 타일/초 단위 이동 속도. 1칸 쿨다운(ms) = 1000 / TileMoveSpeed.
    TileMoveSpeed REAL NOT NULL,

    -- M5 신규 도메인 컬럼 (풀세트 5종 검증) ----------------------------
    -- 닉네임. 미설정 시 NULL — 신규 가입 직후 또는 닉네임 미지정 캐릭터.
    NickName NVARCHAR(32) NULL,

    -- 관리자 플래그. 기본 0(일반 유저). BIT 는 SQL_C_BIT 와 1:1 매핑.
    IsAdmin BIT NOT NULL CONSTRAINT DF_PlayerEntry_IsAdmin DEFAULT 0,

    -- 마지막 접속 시각. ms 정밀도(p=3). 한 번도 접속 안 했으면 NULL.
    LastLoginAt DATETIME2(3) NULL,

    -- 평판 점수. 음수 가능 (-32768 ~ 32767). 기본 0(중립).
    Reputation SMALLINT NOT NULL CONSTRAINT DF_PlayerEntry_Reputation DEFAULT 0,

    -- 아바타 이미지 해시 32-byte. 미설정 시 NULL.
    AvatarHash VARBINARY(32) NULL
);