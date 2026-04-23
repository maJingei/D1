#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBBuildSql.h"
#include "DB/DBConnection.h"
#include "DB/DBStatement.h"

#include <string>

/** 메타데이터 기반 템플릿 CRUD 파사드 (M4). 새 PK 타입은 DBOrmDetail::BindParamPk 오버로드만 추가. */
class DBOrm
{
public:
	/** 모든 컬럼(PK 포함) 을 INSERT. PK 중복 등 실패 시 false. */
	template<typename T>
	static bool Insert(DBConnection& Conn, const T& Entity);

	/** PK 로 1행 SELECT. PkT 는 BindParamPk 오버로드가 지원하는 모든 타입. */
	template<typename T, typename PkT>
	static bool Find(DBConnection& Conn, PkT PkValue, OUT T& OutEntity);

	/** 비-PK 컬럼만 UPDATE. 0행이어도 성공. */
	template<typename T>
	static bool Update(DBConnection& Conn, const T& Entity);

	/** PK 로 1행 DELETE. 0행이어도 성공. */
	template<typename T, typename PkT>
	static bool Delete(DBConnection& Conn, PkT PkValue);
};

namespace DBOrmDetail
{
	/** NOT NULL 문자열 파라미터용 공유 SQL_NTS 슬롯. thread_local 로 수명 무한. */
	inline SQLLEN& GetSqlNtsIndicator()
	{
		static thread_local SQLLEN NtsInd = SQL_NTS;
		NtsInd = SQL_NTS;
		return NtsInd;
	}

	/** ColumnMeta::CType 디스패치로 SQLBindParameter 호출. NOT NULL 문자열은 공유 SQL_NTS 를 indicator 로 넘긴다. */
	inline bool BindParamFromMeta(DBStatement& Stmt, SQLUSMALLINT ParamIndex, const ColumnMeta& Col, void* EntityBase)
	{
		// entity 처음 주소에서 열의 offset만 더해서 src 계산
		char* Src = static_cast<char*>(EntityBase) + Col.Offset;
		SQLLEN* Ind = Col.bNullable ? reinterpret_cast<SQLLEN*>(static_cast<char*>(EntityBase) + Col.IndicatorOffset) : nullptr;

		// NOT NULL 문자열은 ODBC 에 길이 근거(SQL_NTS)를 반드시 제공해야 바인딩이 성립한다.
		if (Ind == nullptr && (Col.CType == ESqlCType::WChar || Col.CType == ESqlCType::Char))
			Ind = &GetSqlNtsIndicator();

		switch (Col.CType)
		{
			case ESqlCType::SBigInt:
				return Stmt.BindParamInt64(ParamIndex, reinterpret_cast<int64*>(Src), Ind);
			case ESqlCType::SLong:
				return Stmt.BindParamInt32(ParamIndex, reinterpret_cast<int32*>(Src), Ind);
			case ESqlCType::SShort:
				return Stmt.BindParamInt16(ParamIndex, reinterpret_cast<int16*>(Src), Ind);
			case ESqlCType::UTinyInt:
				return Stmt.BindParamUInt8(ParamIndex, reinterpret_cast<uint8*>(Src), Ind);
			case ESqlCType::Float:
				return Stmt.BindParamFloat(ParamIndex, reinterpret_cast<float*>(Src), Ind);
			case ESqlCType::Double:
				return Stmt.BindParamDouble(ParamIndex, reinterpret_cast<double*>(Src), Ind);
			case ESqlCType::Bit:
				return Stmt.BindParamBit(ParamIndex, reinterpret_cast<uint8*>(Src), Ind);
			case ESqlCType::WChar:
				return Stmt.BindParamWChar(ParamIndex, reinterpret_cast<wchar_t*>(Src), static_cast<SQLLEN>(Col.Size), static_cast<SQLLEN>(Col.Length), Ind);
			case ESqlCType::Char:
				return Stmt.BindParamChar(ParamIndex, Src, static_cast<SQLLEN>(Col.Size), static_cast<SQLLEN>(Col.Length), Ind);
			case ESqlCType::Date:
				return Stmt.BindParamDate(ParamIndex, reinterpret_cast<SQL_DATE_STRUCT*>(Src), Ind);
			case ESqlCType::Timestamp:
				return Stmt.BindParamTimestamp(ParamIndex, reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(Src), Col.Precision, Ind);
			case ESqlCType::Binary:
				return Stmt.BindParamBinary(ParamIndex, reinterpret_cast<uint8*>(Src), static_cast<SQLLEN>(Col.Size), static_cast<SQLLEN>(Col.Length), Ind);
		}
		return false;
	}

	/** ColumnMeta::CType 디스패치로 SQLBindCol 호출. NULL 가능 컬럼은 Entity 내 _Ind 슬롯을 indicator 로 전달. */
	inline bool BindColFromMeta(DBStatement& Stmt, SQLUSMALLINT ColumnIndex, const ColumnMeta& Col, void* EntityBase)
	{
		char* Dst = static_cast<char*>(EntityBase) + Col.Offset;
		SQLLEN* Ind = Col.bNullable ? reinterpret_cast<SQLLEN*>(static_cast<char*>(EntityBase) + Col.IndicatorOffset) : nullptr;

		switch (Col.CType)
		{
			case ESqlCType::SBigInt:
				return Stmt.BindColInt64(ColumnIndex, reinterpret_cast<int64*>(Dst), Ind);
			case ESqlCType::SLong:
				return Stmt.BindColInt32(ColumnIndex, reinterpret_cast<int32*>(Dst), Ind);
			case ESqlCType::SShort:
				return Stmt.BindColInt16(ColumnIndex, reinterpret_cast<int16*>(Dst), Ind);
			case ESqlCType::UTinyInt:
				return Stmt.BindColUInt8(ColumnIndex, reinterpret_cast<uint8*>(Dst), Ind);
			case ESqlCType::Float:
				return Stmt.BindColFloat(ColumnIndex, reinterpret_cast<float*>(Dst), Ind);
			case ESqlCType::Double:
				return Stmt.BindColDouble(ColumnIndex, reinterpret_cast<double*>(Dst), Ind);
			case ESqlCType::Bit:
				return Stmt.BindColBit(ColumnIndex, reinterpret_cast<uint8*>(Dst), Ind);
			case ESqlCType::WChar:
				return Stmt.BindColWChar(ColumnIndex, reinterpret_cast<wchar_t*>(Dst), static_cast<SQLLEN>(Col.Size), Ind);
			case ESqlCType::Char:
				return Stmt.BindColChar(ColumnIndex, Dst, static_cast<SQLLEN>(Col.Size), Ind);
			case ESqlCType::Date:
				return Stmt.BindColDate(ColumnIndex, reinterpret_cast<SQL_DATE_STRUCT*>(Dst), Ind);
			case ESqlCType::Timestamp:
				return Stmt.BindColTimestamp(ColumnIndex, reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(Dst), Ind);
			case ESqlCType::Binary:
				return Stmt.BindColBinary(ColumnIndex, reinterpret_cast<uint8*>(Dst), static_cast<SQLLEN>(Col.Size), Ind);
		}
		return false;
	}

	/** PK 바인딩용 스택 스토리지. Find/Delete 의 지역 스코프에 1개 선언해 BindParamPk 에 참조로 넘긴다. */
	struct PkBindStorage
	{
		int64 Int64Buf = 0;
		SQLLEN NtsInd = SQL_NTS;
	};

	/** BIGINT PK. uint64 → int64 복사본을 Storage 에 두고 그 주소를 바인딩. */
	inline bool BindParamPk(DBStatement& Stmt, SQLUSMALLINT ParamIndex, const ColumnMeta& /*PkCol*/, uint64 PkValue, PkBindStorage& Storage)
	{
		Storage.Int64Buf = static_cast<int64>(PkValue);
		return Stmt.BindParamInt64(ParamIndex, &Storage.Int64Buf);
	}

	/** NVARCHAR(N) PK. PkValue 는 null-term 문자열 — 호출자가 Execute 까지 수명 보장. */
	inline bool BindParamPk(DBStatement& Stmt, SQLUSMALLINT ParamIndex, const ColumnMeta& PkCol, const wchar_t* PkValue, PkBindStorage& Storage)
	{
		Storage.NtsInd = SQL_NTS;
		return Stmt.BindParamWChar(ParamIndex,
			const_cast<wchar_t*>(PkValue),
			static_cast<SQLLEN>(PkCol.Size),
			static_cast<SQLLEN>(PkCol.Length),
			&Storage.NtsInd);
	}

	/** VARCHAR(N) PK. Account.Id 처럼 char 기반 natural key 를 쓰는 엔티티용. */
	inline bool BindParamPk(DBStatement& Stmt, SQLUSMALLINT ParamIndex, const ColumnMeta& PkCol, const char* PkValue, PkBindStorage& Storage)
	{
		Storage.NtsInd = SQL_NTS;
		return Stmt.BindParamChar(ParamIndex,
			const_cast<char*>(PkValue),
			static_cast<SQLLEN>(PkCol.Size),
			static_cast<SQLLEN>(PkCol.Length),
			&Storage.NtsInd);
	}
}

template<typename T>
bool DBOrm::Insert(DBConnection& Conn, const T& Entity)
{
	// 1. insert SQL 텍스트 생성. statement에 캐시되어 있으면 캐시 반환
	DBStatement* Stmt = Conn.GetOrPrepare(BuildInsertSql<T>());
	if (Stmt == nullptr)
		return false;

	// 2. 직전 호출의 커서 잔재 정리. 첫 사용이거나 INSERT 라 결과셋이 없어도 SQL_CLOSE 는 no-op 으로 안전.
	if (Stmt->Reset() == false)
		return false;

	// 3. 생성된 sql구문의 ?에 T타입의 메타데이터를 가져와서 바인딩. 같은 슬롯 재바인딩은 ODBC 가 자동 덮어씀.
	const auto& Meta = GetTableMetadata<T>();
	void* EntityBase = const_cast<void*>(static_cast<const void*>(&Entity));
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::BindParamFromMeta(*Stmt, static_cast<SQLUSMALLINT>(i + 1), Meta.Columns[i], EntityBase) == false)
			return false;
	}

	return Stmt->Execute();
}

template<typename T, typename PkT>
bool DBOrm::Find(DBConnection& Conn, PkT PkValue, T& OutEntity)
{
	DBStatement* Stmt = Conn.GetOrPrepare(BuildSelectByPkSql<T>());
	if (Stmt == nullptr)
		return false;

	// SELECT 는 결과셋을 남기므로 직전 호출이 있었다면 SQL_CLOSE 가 핵심 — 없으면 "Function sequence error" 발생.
	if (Stmt->Reset() == false)
		return false;

	const auto& Meta = GetTableMetadata<T>();
	const ColumnMeta& PkCol = Meta.Columns[Meta.PkIndex];
	DBOrmDetail::PkBindStorage Storage;
	if (DBOrmDetail::BindParamPk(*Stmt, 1, PkCol, PkValue, Storage) == false)
		return false;

	void* EntityBase = static_cast<void*>(&OutEntity);
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::BindColFromMeta(*Stmt, static_cast<SQLUSMALLINT>(i + 1), Meta.Columns[i], EntityBase) == false)
			return false;
	}

	if (Stmt->Execute() == false)
		return false;

	return Stmt->Fetch();
}

template<typename T>
bool DBOrm::Update(DBConnection& Conn, const T& Entity)
{
	DBStatement* Stmt = Conn.GetOrPrepare(BuildUpdateSql<T>());
	if (Stmt == nullptr)
		return false;

	if (Stmt->Reset() == false)
		return false;

	const auto& Meta = GetTableMetadata<T>();
	void* EntityBase = const_cast<void*>(static_cast<const void*>(&Entity));

	// SET 절: PK 제외 전 컬럼을 1..N-1 슬롯에 바인딩.
	SQLUSMALLINT ParamIdx = 1;
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i == Meta.PkIndex) continue;
		if (DBOrmDetail::BindParamFromMeta(*Stmt, ParamIdx++, Meta.Columns[i], EntityBase) == false)
			return false;
	}

	// WHERE Pk = ? — 메타 디스패치 경유라 BIGINT/NVARCHAR PK 모두 자동 지원.
	if (DBOrmDetail::BindParamFromMeta(*Stmt, ParamIdx, Meta.Columns[Meta.PkIndex], EntityBase) == false)
		return false;

	return Stmt->Execute();
}

template<typename T, typename PkT>
bool DBOrm::Delete(DBConnection& Conn, PkT PkValue)
{
	DBStatement* Stmt = Conn.GetOrPrepare(BuildDeleteByPkSql<T>());
	if (Stmt == nullptr)
		return false;

	if (Stmt->Reset() == false)
		return false;

	const auto& Meta = GetTableMetadata<T>();
	const ColumnMeta& PkCol = Meta.Columns[Meta.PkIndex];
	DBOrmDetail::PkBindStorage Storage;
	if (DBOrmDetail::BindParamPk(*Stmt, 1, PkCol, PkValue, Storage) == false)
		return false;

	return Stmt->Execute();
}