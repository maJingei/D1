#pragma once

#include "CoreMinimal.h"

#include <cmath>

/**
 * 2D 부동소수점 벡터.
 * 위치, 방향, 오프셋 등 2차원 좌표 연산에 사용한다.
 */
struct FVector2D
{
	float X = 0.f;
	float Y = 0.f;

	FVector2D() = default;
	FVector2D(float InX, float InY) : X(InX), Y(InY) {}

	FVector2D operator+(const FVector2D& Other) const { return { X + Other.X, Y + Other.Y }; }
	FVector2D operator-(const FVector2D& Other) const { return { X - Other.X, Y - Other.Y }; }
	FVector2D operator*(float Scalar) const { return { X * Scalar, Y * Scalar }; }

	FVector2D& operator+=(const FVector2D& Other) { X += Other.X; Y += Other.Y; return *this; }
	FVector2D& operator-=(const FVector2D& Other) { X -= Other.X; Y -= Other.Y; return *this; }

	/** 벡터 길이 */
	float Length() const { return std::sqrtf(X * X + Y * Y); }

	/** 길이의 제곱 (sqrt 없이 비교할 때 사용) */
	float LengthSquared() const { return X * X + Y * Y; }

	/** 단위 벡터 반환. 길이가 0이면 영벡터 반환. */
	FVector2D Normalized() const
	{
		float Len = Length();
		return Len > 0.f ? FVector2D{ X / Len, Y / Len } : FVector2D{};
	}

	static FVector2D Zero() { return { 0.f, 0.f }; }
};
