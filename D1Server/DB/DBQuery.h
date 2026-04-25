#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBMeta.h"

#include <memory>
#include <string>
#include <vector>

#include <sql.h>
#include <sqlext.h>

class DBConnection;
class DBStatement;

template<typename T>
class DBSet;

// LiteralValue — 우변 리터럴 저장소 (C# ConstantExpression 대체). 태그드 유니온으로 값 소유 → Execute 시점까지 수명 보장.

struct LiteralValue
{
	enum class EKind : uint8
	{
		Int64,
		Int32,
		Int16,
		UInt8,
		Float,
		Double,
		Bit,
		WideString,
		NarrowString,
	};

	EKind Kind = EKind::Int64;

	int64 Int64Value = 0;
	int32 Int32Value = 0;
	int16 Int16Value = 0;
	uint8 UInt8Value = 0;
	float FloatValue = 0.0f;
	double DoubleValue = 0.0;
	std::wstring WideStringValue;
	std::string NarrowStringValue;

	// 문자열 바인딩용 SQL_NTS indicator. ODBC 가 Execute 시점에 포인터로 읽어가므로
	// LiteralValue 와 수명을 공유해야 한다 (ExprNode 가 값-소유하니 자동 OK).
	mutable SQLLEN StringLengthIndicator = SQL_NTS;
};


// ExprNode — AST 이진트리 노드. Comparison(리프) + Logical(내부, 좌우 shared_ptr 자식).

enum class EComparisonOp : uint8
{
	Equal,
	NotEqual,
	LessThan,
	GreaterThan,
	LessOrEqual,
	GreaterOrEqual,
};

enum class ELogicalOp : uint8
{
	And,
	Or,
};

struct ExprNode
{
	enum class EKind : uint8
	{
		Comparison, // 한개의 비교식
		Logical, // 논리 연결식
	};

	// 한개의 비교식일 때 (< > != == 이런 표현식)
	EKind Kind = EKind::Comparison;

	// Comparison (leaf) — Kind == Comparison 일 때만 유효.
	size_t ColumnIndex = 0;
	EComparisonOp ComparisonOp = EComparisonOp::Equal;
	LiteralValue Literal;

	// Logical (internal) — Kind == Logical 일 때만 유효.
	ELogicalOp LogicalOp = ELogicalOp::And;
	
	// ExprNode 링크드 리스트
	std::shared_ptr<ExprNode> Left;
	std::shared_ptr<ExprNode> Right;
};


// OrderTerm — ORDER BY 정렬 키 1개. 순서 있는 리스트라 트리 아닌 벡터로 누적.

struct OrderTerm
{
	size_t ColumnIndex = 0;
	bool bDescending = false;
};


// ColumnProxy<T, Idx> — 좌변 플레이스홀더. empty 구조체, 타입 레벨로 "T 의 Idx 번째 컬럼" 정보만 전달.

template<typename T, size_t Idx>
struct ColumnProxy
{
	static constexpr const ColumnMeta& Meta() { return TableMetadata<T>::Columns[Idx]; }
	static constexpr size_t Index() { return Idx; }
	using Entity = T;
};


// 연산자 오버로드 — operator X(ColumnProxy, 리터럴) → ExprNode{Comparison} 반환. 타입별 6종 연산자 매크로로 생성.

namespace DBQueryDetail
{
	template<typename T, size_t Idx>
	inline ExprNode MakeComparisonInt64(EComparisonOp Op, int64 Value)
	{
		static_assert(Idx < TableMetadata<T>::NumColumns, "ColumnProxy index out of range");
		ExprNode Node;
		Node.Kind = ExprNode::EKind::Comparison;
		Node.ColumnIndex = Idx;
		Node.ComparisonOp = Op;
		Node.Literal.Kind = LiteralValue::EKind::Int64;
		Node.Literal.Int64Value = Value;
		return Node;
	}

	template<typename T, size_t Idx>
	inline ExprNode MakeComparisonInt32(EComparisonOp Op, int32 Value)
	{
		ExprNode Node;
		Node.Kind = ExprNode::EKind::Comparison;
		Node.ColumnIndex = Idx;
		Node.ComparisonOp = Op;
		Node.Literal.Kind = LiteralValue::EKind::Int32;
		Node.Literal.Int32Value = Value;
		return Node;
	}

	template<typename T, size_t Idx>
	inline ExprNode MakeComparisonInt16(EComparisonOp Op, int16 Value)
	{
		ExprNode Node;
		Node.Kind = ExprNode::EKind::Comparison;
		Node.ColumnIndex = Idx;
		Node.ComparisonOp = Op;
		Node.Literal.Kind = LiteralValue::EKind::Int16;
		Node.Literal.Int16Value = Value;
		return Node;
	}

	template<typename T, size_t Idx>
	inline ExprNode MakeComparisonUInt8(EComparisonOp Op, uint8 Value)
	{
		ExprNode Node;
		Node.Kind = ExprNode::EKind::Comparison;
		Node.ColumnIndex = Idx;
		Node.ComparisonOp = Op;
		Node.Literal.Kind = LiteralValue::EKind::UInt8;
		Node.Literal.UInt8Value = Value;
		return Node;
	}

	template<typename T, size_t Idx>
	inline ExprNode MakeComparisonFloat(EComparisonOp Op, float Value)
	{
		ExprNode Node;
		Node.Kind = ExprNode::EKind::Comparison;
		Node.ColumnIndex = Idx;
		Node.ComparisonOp = Op;
		Node.Literal.Kind = LiteralValue::EKind::Float;
		Node.Literal.FloatValue = Value;
		return Node;
	}

	template<typename T, size_t Idx>
	inline ExprNode MakeComparisonWideString(EComparisonOp Op, const wchar_t* Value)
	{
		ExprNode Node;
		Node.Kind = ExprNode::EKind::Comparison;
		Node.ColumnIndex = Idx;
		Node.ComparisonOp = Op;
		Node.Literal.Kind = LiteralValue::EKind::WideString;
		Node.Literal.WideStringValue = Value ? Value : L"";
		return Node;
	}

	template<typename T, size_t Idx>
	inline ExprNode MakeComparisonNarrowString(EComparisonOp Op, const char* Value)
	{
		ExprNode Node;
		Node.Kind = ExprNode::EKind::Comparison;
		Node.ColumnIndex = Idx;
		Node.ComparisonOp = Op;
		Node.Literal.Kind = LiteralValue::EKind::NarrowString;
		Node.Literal.NarrowStringValue = Value ? Value : "";
		return Node;
	}
}

// ─── 비교 연산자 — 5 종 스칼라 × 6 종 연산자 ─────────────────────
#define DB_QUERY_DEFINE_SCALAR_OPS(ScalarType, MakeFn)                                                                                                        \
template<typename T, size_t Idx> inline ExprNode operator==(ColumnProxy<T, Idx>, ScalarType Value) { return DBQueryDetail::MakeFn<T, Idx>(EComparisonOp::Equal,          Value); } \
template<typename T, size_t Idx> inline ExprNode operator!=(ColumnProxy<T, Idx>, ScalarType Value) { return DBQueryDetail::MakeFn<T, Idx>(EComparisonOp::NotEqual,       Value); } \
template<typename T, size_t Idx> inline ExprNode operator< (ColumnProxy<T, Idx>, ScalarType Value) { return DBQueryDetail::MakeFn<T, Idx>(EComparisonOp::LessThan,       Value); } \
template<typename T, size_t Idx> inline ExprNode operator> (ColumnProxy<T, Idx>, ScalarType Value) { return DBQueryDetail::MakeFn<T, Idx>(EComparisonOp::GreaterThan,    Value); } \
template<typename T, size_t Idx> inline ExprNode operator<=(ColumnProxy<T, Idx>, ScalarType Value) { return DBQueryDetail::MakeFn<T, Idx>(EComparisonOp::LessOrEqual,    Value); } \
template<typename T, size_t Idx> inline ExprNode operator>=(ColumnProxy<T, Idx>, ScalarType Value) { return DBQueryDetail::MakeFn<T, Idx>(EComparisonOp::GreaterOrEqual, Value); }

DB_QUERY_DEFINE_SCALAR_OPS(int64, MakeComparisonInt64)
DB_QUERY_DEFINE_SCALAR_OPS(int32, MakeComparisonInt32)
DB_QUERY_DEFINE_SCALAR_OPS(int16, MakeComparisonInt16)
DB_QUERY_DEFINE_SCALAR_OPS(uint8, MakeComparisonUInt8)
DB_QUERY_DEFINE_SCALAR_OPS(float, MakeComparisonFloat)

#undef DB_QUERY_DEFINE_SCALAR_OPS

// ─── 비교 연산자 — 문자열 (NVARCHAR / VARCHAR) ──────────────────
// 문자열은 == / != 만 제공 — 게임 로직상 < > 비교 실용도 낮음.
template<typename T, size_t Idx> 
inline ExprNode operator==(ColumnProxy<T, Idx>, const wchar_t* Value)
{
	return DBQueryDetail::MakeComparisonWideString<T, Idx>(EComparisonOp::Equal, Value);
}

template<typename T, size_t Idx> 
inline ExprNode operator!=(ColumnProxy<T, Idx>, const wchar_t* Value)
{
	return DBQueryDetail::MakeComparisonWideString<T, Idx>(EComparisonOp::NotEqual, Value);
}

template<typename T, size_t Idx> 
inline ExprNode operator==(ColumnProxy<T, Idx>, const char* Value)
{
	return DBQueryDetail::MakeComparisonNarrowString<T, Idx>(EComparisonOp::Equal, Value);
}

template<typename T, size_t Idx> 
inline ExprNode operator!=(ColumnProxy<T, Idx>, const char* Value)
{
	return DBQueryDetail::MakeComparisonNarrowString<T, Idx>(EComparisonOp::NotEqual, Value);
}

// ─── 논리 연산자 — ExprNode op ExprNode ──────────────────────────
// 트리의 내부 노드(Logical) 생성. 좌·우변을 shared_ptr 로 승격해 얕은 복사.
inline ExprNode operator&&(ExprNode Left, ExprNode Right)
{
	ExprNode Node;
	Node.Kind = ExprNode::EKind::Logical;
	Node.LogicalOp = ELogicalOp::And;
	Node.Left = std::make_shared<ExprNode>(std::move(Left));
	Node.Right = std::make_shared<ExprNode>(std::move(Right));
	return Node;
}

inline ExprNode operator||(ExprNode Left, ExprNode Right)
{
	ExprNode Node;
	Node.Kind = ExprNode::EKind::Logical;
	Node.LogicalOp = ELogicalOp::Or;
	Node.Left = std::make_shared<ExprNode>(std::move(Left));
	Node.Right = std::make_shared<ExprNode>(std::move(Right));
	return Node;
}


/**
 * 체인의 상태를 저장하고 내부에 pred들을 저장한다.
 */
template<typename T>
class IQueryable
{
public:
	IQueryable(DBSet<T>& InSourceSet) : SourceSet(InSourceSet) {}

	// Where 누적 — 기존 Predicate 와 AND 로 결합.
	IQueryable& Where(ExprNode Predicate)
	{
		// 1. 첫 표현식이면
		if (RootPredicate == nullptr)
		{
			RootPredicate = std::make_shared<ExprNode>(std::move(Predicate));
			return *this;
		}

		// 2. 이미 Predicate 가 있으면 AND 로 결합 — chained Where(...).Where(...) 경로.
		ExprNode CombinedPredicate;
		CombinedPredicate.Kind = ExprNode::EKind::Logical;
		CombinedPredicate.LogicalOp = ELogicalOp::And;
		CombinedPredicate.Left = RootPredicate;
		CombinedPredicate.Right = std::make_shared<ExprNode>(std::move(Predicate));
		RootPredicate = std::make_shared<ExprNode>(std::move(CombinedPredicate));
		return *this;
	}

	// 정렬 키 누적 — 컴파일타임 Idx 를 OrderTerm 에 기록.
	template<size_t Idx>
	IQueryable& OrderBy(ColumnProxy<T, Idx>)
	{
		OrderTerms.push_back({ Idx, false });
		return *this;
	}

	template<size_t Idx>
	IQueryable& OrderByDescending(ColumnProxy<T, Idx>)
	{
		OrderTerms.push_back({ Idx, true }); 
		return *this;
	}

	// OrderBy 정렬과 더불어서 추가적인 정렬이 필요할 때 ThenBy 사용
	template<size_t Idx>
	IQueryable& ThenBy(ColumnProxy<T, Idx>)
	{
		OrderTerms.push_back({ Idx, false });
		return *this;
	}

	template<size_t Idx>
	IQueryable& ThenByDescending(ColumnProxy<T, Idx>) 
	{ 
		OrderTerms.push_back({ Idx, true });
		return *this; 
	}

	IQueryable& AsNoTracking()
	{
		bNoTracking = true;
		return *this;
	}

	// ─── 종결자 (DBSet.h 말미에서 정의) ─────────────────────────
	T* FirstOrDefault();
	T* SingleOrDefault();
	std::vector<T*> ToList();

	// Count: 누적 WHERE 절을 그대로 사용. 결과 0행이면 0 반환.
	int64 Count();

	// Sum: 컬럼 타입에 따라 반환 타입 자동 결정 (정수 → int64, float/double → double).
	template<size_t Idx>
	auto Sum(ColumnProxy<T, Idx> Column);

	// ─── 빌더/실행 루틴이 소비하는 getter ─────────────────────────
	const ExprNode* GetRootPredicate() const { return RootPredicate.get(); }
	const std::vector<OrderTerm>& GetOrderTerms() const { return OrderTerms; }
	bool IsNoTracking() const { return bNoTracking; }
	DBSet<T>& GetSourceSet() const { return SourceSet; }

private:
	DBSet<T>& SourceSet;
	
	std::shared_ptr<ExprNode> RootPredicate;
	std::vector<OrderTerm> OrderTerms;
	bool bNoTracking = false;
};