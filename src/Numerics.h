#pragma once

#include "Common.h"

struct Vector3F
{
	float x;
	float y;
	float z;

	static float DotProduct(const Vector3F* left, const Vector3F* right);
	static float Length(const Vector3F* vector);
};

struct Vector3I
{
	int32_t x;
	int32_t y;
	int32_t z;
};

struct Vector2F
{
	float x;
	float y;
};

struct Vector2I
{
	int32_t x;
	int32_t y;
};

struct Vector3I16
{
	int16_t x;
	int16_t y;
	int16_t z;
};

struct Point2I16
{
	int16_t x;
	int16_t y;
};

struct Angles
{
	uint16_t pitch;
	uint16_t yaw;
};

struct PosAndAngles
{
	Vector3I pos;
	Angles angles;
};

struct PointI
{
	int32_t x;
	int32_t y;
};

struct Plane
{
	Vector3F normal;
	float distance;
};

struct Matrix3x3I16
{
	int16_t m00;
	int16_t m01;
	int16_t m02;
	int16_t m10;
	int16_t m11;
	int16_t m12;
	int16_t m20;
	int16_t m21;
	int16_t m22;
};

namespace Numerics
{
	extern float* g_trigLUT;
	extern int16_t g_fixedTrigLUT[0x1000];

	int32_t RoundUpToPowerOf2(int32_t number);
	void InitTrigLut();
}
