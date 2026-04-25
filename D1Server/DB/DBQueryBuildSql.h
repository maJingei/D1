#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBBuildSql.h"   
#include "DB/DBMeta.h"
#include "DB/DBQuery.h"
#include "DB/DBStatement.h"

#include <string>
#include <vector>

/**
 * ? 하나에 바인딩할 재료 세트.
 *   Literal: 값 슬롯 (태그드 유니온)
 *   Column : 같은 비교에 참여한 테이블 컬럼 메타 — 문자열/바이너리 타입의 버퍼 크기/컬럼 크기 유도용
 */
struct QueryParameterBinding
{
	const LiteralValue* Literal = nullptr;
	const ColumnMeta* Column = nullptr;
};


namespace DBQueryBuildSqlDetail
{
	// ─── EComparisonOp → SQL 연산자 문자열 ───────────────────────
	inline const wchar_t* ToSqlOperator(EComparisonOp Op)
	{
		switch (Op)
		{
			case EComparisonOp::Equal:          return L"=";
			case EComparisonOp::NotEqual:       return L"<>";
			case EComparisonOp::LessThan:       return L"<";
			case EComparisonOp::GreaterThan:    return L">";
			case EComparisonOp::LessOrEqual:    return L"<=";
			case EComparisonOp::GreaterOrEqual: return L">=";
		}
		return L"=";
	}

	// ─── ELogicalOp → SQL 연산자 문자열 ──────────────────────────
	inline const wchar_t* ToSqlOperator(ELogicalOp Op)
	{
		switch (Op)
		{
			case ELogicalOp::And: return L" AND ";
			case ELogicalOp::Or:  return L" OR ";
		}
		return L" AND ";
	}

	// ─── WHERE 재귀 워커 ─────────────────────────────────────────
	/**
	 * Comparison 리프: "(<ColName> <op> ?)" 출력 + QueryParameterBinding push_back.
	 * Logical 내부: "(" + EmitNode(Left) + " AND/OR " + EmitNode(Right) + ")"
	 * 괄호를 모든 노드에 두르는 이유: 우선순위 혼동 0. SQL 엔진이 최적화해도 표현력은 무변.
	 */
	template<typename T>
	inline void EmitWhereNode(const ExprNode& Node, std::wstring& OutSql, std::vector<QueryParameterBinding>& OutBindings)
	{
		const auto& TableMeta = GetTableMetadata<T>();

		if (Node.Kind == ExprNode::EKind::Comparison)
		{
			const ColumnMeta& ColumnMetaRef = TableMeta.Columns[Node.ColumnIndex];
			OutSql.append(L"(");
			// 이름 연결
			AppendNarrowAsWide(OutSql, ColumnMetaRef.Name);
			
			// op에 따른 operation 연결
			OutSql.append(L" ").append(ToSqlOperator(Node.ComparisonOp)).append(L" ?)");
			OutBindings.push_back(QueryParameterBinding{ &Node.Literal, &ColumnMetaRef });
			return;
		}

		// 트리 중위 순회로 재귀하며 linq 체이닝 쿼리 작성
		OutSql.append(L"(");
		EmitWhereNode<T>(*Node.Left, OutSql, OutBindings);
		OutSql.append(ToSqlOperator(Node.LogicalOp));
		EmitWhereNode<T>(*Node.Right, OutSql, OutBindings);
		OutSql.append(L")");
	}
}


// ─── SELECT SQL 조립 진입점 ──────────────────────────────────────
/**
 * "SELECT [TOP N] Col1, Col2, ... FROM <Table> [WHERE ...] [ORDER BY ...];" 전체 조립.
 * OptionalPredicate = nullptr 이면 WHERE 절 생략. OrderTerms 비어있으면 ORDER BY 생략.
 * TopLimit = 0 이면 TOP 절 생략 (ToList 경로).
 * 생성된 SQL 텍스트는 GetOrPrepare 에 그대로 넘길 수 있다.
 */
template<typename T>
inline void BuildSelectSql(const ExprNode* OptionalPredicate, const std::vector<OrderTerm>& OrderTerms, size_t TopLimit, std::wstring& OutSql, std::vector<QueryParameterBinding>& OutBindings)
{
	const auto& TableMeta = GetTableMetadata<T>();

	OutSql.clear();
	OutSql.reserve(256);

	// 1. SELECT [TOP N] 컬럼리스트
	OutSql.append(L"SELECT ");
	if (TopLimit > 0)
	{
		OutSql.append(L"TOP ").append(std::to_wstring(TopLimit)).append(L" ");
	}
	for (size_t i = 0; i < TableMeta.NumColumns; ++i)
	{
		if (i > 0)
		{
			OutSql.append(L", ");
		}
		AppendNarrowAsWide(OutSql, TableMeta.Columns[i].Name);
	}

	// 2. FROM
	OutSql.append(L" FROM ");
	AppendNarrowAsWide(OutSql, TableMeta.TableName);

	// 3. WHERE (optional) — 재귀로 where 내부 채우기
	if (OptionalPredicate != nullptr)
	{
		OutSql.append(L" WHERE ");
		DBQueryBuildSqlDetail::EmitWhereNode<T>(*OptionalPredicate, OutSql, OutBindings);
	}

	// 4. ORDER BY (optional) — 선언 순서 유지.
	if (OrderTerms.empty() == false)
	{
		OutSql.append(L" ORDER BY ");
		for (size_t i = 0; i < OrderTerms.size(); ++i)
		{
			const OrderTerm& Term = OrderTerms[i];
			if (i > 0)
			{
				OutSql.append(L", ");
			}
			AppendNarrowAsWide(OutSql, TableMeta.Columns[Term.ColumnIndex].Name);
			OutSql.append(Term.bDescending ? L" DESC" : L" ASC");
		}
	}

	OutSql.append(L";");
}


// ─── 집계 SQL 조립 (M7.1) ──────────────────────────────────────
/**
 * "SELECT COUNT(*) FROM <Table> [WHERE ...];" 조립.
 * OptionalPredicate = nullptr 이면 WHERE 절 생략 — 전체 행 카운트.
 * 반환값은 1행×1열(int64) — IQueryable<T>::Count() 에서 GetColumnInt64 로 수신.
 */
template<typename T>
inline void BuildCountSql(const ExprNode* OptionalPredicate, std::wstring& OutSql, std::vector<QueryParameterBinding>& OutBindings)
{
	const auto& TableMeta = GetTableMetadata<T>();

	OutSql.clear();
	OutSql.reserve(128);

	OutSql.append(L"SELECT COUNT(*) FROM ");
	AppendNarrowAsWide(OutSql, TableMeta.TableName);

	if (OptionalPredicate != nullptr)
	{
		OutSql.append(L" WHERE ");
		DBQueryBuildSqlDetail::EmitWhereNode<T>(*OptionalPredicate, OutSql, OutBindings);
	}

	OutSql.append(L";");
}

/**
 * "SELECT SUM(<Col>) FROM <Table> [WHERE ...];" 조립.
 * ColumnIndex 는 TableMetadata<T>::Columns[Idx] 의 Idx. 호출자가 ColumnProxy<T, Idx> 로 지정.
 * SUM 은 행이 0개여도 NULL 대신 NULL 을 반환하므로 호출부가 GetColumnInt64/Double 의 NULL→0 정규화 로직에 의존.
 */
template<typename T>
inline void BuildSumSql(size_t ColumnIndex, const ExprNode* OptionalPredicate, std::wstring& OutSql, std::vector<QueryParameterBinding>& OutBindings)
{
	const auto& TableMeta = GetTableMetadata<T>();

	OutSql.clear();
	OutSql.reserve(128);

	OutSql.append(L"SELECT SUM(");
	AppendNarrowAsWide(OutSql, TableMeta.Columns[ColumnIndex].Name);
	OutSql.append(L") FROM ");
	AppendNarrowAsWide(OutSql, TableMeta.TableName);

	if (OptionalPredicate != nullptr)
	{
		OutSql.append(L" WHERE ");
		DBQueryBuildSqlDetail::EmitWhereNode<T>(*OptionalPredicate, OutSql, OutBindings);
	}

	OutSql.append(L";");
}


// ─── 리터럴 바인딩 ────────────────────────────────────────────────
/**
 * 수집된 QueryParameterBinding 을 순서대로 prepared DBStatement 에 바인딩.
 * LiteralValue::Kind 로 디스패치, 문자열 타입은 ColumnMeta 의 Length/Size 를 참조.
 *
 * const_cast 사용처: std::wstring::c_str() 이 const wchar_t* 를 주지만 ODBC 의
 * BindParamWChar 는 non-const wchar_t* 를 요구한다. ODBC 는 input 바인딩에서
 * 버퍼를 읽기만 하므로 const_cast 안전.
 */
inline bool BindLiteralsToStatement(DBStatement& Statement, const std::vector<QueryParameterBinding>& Bindings)
{
	for (size_t i = 0; i < Bindings.size(); ++i)
	{
		const SQLUSMALLINT ParamIndex = static_cast<SQLUSMALLINT>(i + 1);
		const LiteralValue* Literal = Bindings[i].Literal;
		const ColumnMeta* Column = Bindings[i].Column;

		switch (Literal->Kind)
		{
			case LiteralValue::EKind::Int64:
				if (Statement.BindParamInt64(ParamIndex, const_cast<int64*>(&Literal->Int64Value)) == false) return false;
				break;

			case LiteralValue::EKind::Int32:
				if (Statement.BindParamInt32(ParamIndex, const_cast<int32*>(&Literal->Int32Value)) == false) return false;
				break;

			case LiteralValue::EKind::Int16:
				if (Statement.BindParamInt16(ParamIndex, const_cast<int16*>(&Literal->Int16Value)) == false) return false;
				break;

			case LiteralValue::EKind::UInt8:
				if (Statement.BindParamUInt8(ParamIndex, const_cast<uint8*>(&Literal->UInt8Value)) == false) return false;
				break;

			case LiteralValue::EKind::Float:
				if (Statement.BindParamFloat(ParamIndex, const_cast<float*>(&Literal->FloatValue)) == false) return false;
				break;

			case LiteralValue::EKind::Double:
				if (Statement.BindParamDouble(ParamIndex, const_cast<double*>(&Literal->DoubleValue)) == false) return false;
				break;

			case LiteralValue::EKind::Bit:
				if (Statement.BindParamBit(ParamIndex, const_cast<uint8*>(&Literal->UInt8Value)) == false) return false;
				break;

			case LiteralValue::EKind::WideString:
			{
				// NVARCHAR(N) — BufferLength = (size+1)*2 byte, ColumnSize = ColumnMeta::Length (= N).
				Literal->StringLengthIndicator = SQL_NTS;
				wchar_t* ValuePtr = const_cast<wchar_t*>(Literal->WideStringValue.c_str());
				const SQLLEN BufferLengthBytes = static_cast<SQLLEN>((Literal->WideStringValue.size() + 1) * sizeof(wchar_t));
				const SQLLEN ColumnSizeChars = static_cast<SQLLEN>(Column->Length);
				if (Statement.BindParamWChar(ParamIndex, ValuePtr, BufferLengthBytes, ColumnSizeChars, &Literal->StringLengthIndicator) == false) return false;
				break;
			}

			case LiteralValue::EKind::NarrowString:
			{
				// VARCHAR(N) — BufferLength = size+1 byte, ColumnSize = ColumnMeta::Length (= N).
				Literal->StringLengthIndicator = SQL_NTS;
				char* ValuePtr = const_cast<char*>(Literal->NarrowStringValue.c_str());
				const SQLLEN BufferLengthBytes = static_cast<SQLLEN>(Literal->NarrowStringValue.size() + 1);
				const SQLLEN ColumnSizeBytes = static_cast<SQLLEN>(Column->Length);
				if (Statement.BindParamChar(ParamIndex, ValuePtr, BufferLengthBytes, ColumnSizeBytes, &Literal->StringLengthIndicator) == false) return false;
				break;
			}
		}
	}

	return true;
}