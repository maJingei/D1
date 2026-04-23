-- D1Server 테스트용 통합 스키마. SSMS 에서 전체 실행하면 DROP → CREATE 순서로 재구성된다.
-- 테이블당 개별 .sql 을 만들지 않고 이 파일에 계속 append 하는 규약.

-- =====================================================================
-- dbo.PlayerEntry — M2 수동 CRUD 타겟, M5 풀세트 5종 검증 필드 포함.
-- =====================================================================

IF OBJECT_ID('dbo.PlayerEntry', 'U') IS NOT NULL
    DROP TABLE dbo.PlayerEntry;

CREATE TABLE dbo.PlayerEntry
(
    PlayerID BIGINT NOT NULL PRIMARY KEY,
    CharacterType INT NOT NULL,
    LevelID INT NOT NULL,
    TileX INT NOT NULL,
    TileY INT NOT NULL,
    HP INT NOT NULL,
    MaxHP INT NOT NULL,
    AttackDamage INT NOT NULL,
    TileMoveSpeed REAL NOT NULL,
    NickName NVARCHAR(32) NULL,
    IsAdmin BIT NOT NULL CONSTRAINT DF_PlayerEntry_IsAdmin DEFAULT 0,
    LastLoginAt DATETIME2(3) NULL,
    Reputation SMALLINT NOT NULL CONSTRAINT DF_PlayerEntry_Reputation DEFAULT 0,
    AvatarHash VARBINARY(32) NULL
);

-- =====================================================================
-- dbo.Account — M4.5 로그인 통합 신규 테이블.
-- Id 는 NVARCHAR(32) natural key PK, PlayerID 는 PlayerEntry.PlayerID 에 대한 논리적 FK.
-- IDENTITY 미사용 — 서버가 AccountDb::SelectMaxPlayerID seed + NextPlayerID 카운터로 발급.
-- =====================================================================

IF OBJECT_ID('dbo.Account', 'U') IS NOT NULL
    DROP TABLE dbo.Account;

CREATE TABLE dbo.Account
(
    Id VARCHAR(32) NOT NULL PRIMARY KEY,
    Password VARCHAR(32) NOT NULL,
    PlayerID BIGINT NOT NULL
);
