#include "Nu3D/Particles.h"
#include "Nu3D/Camera.h"
#include "AudioManager/AudioManager.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Toy2.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace Nu3D
{
	namespace Particles
	{
		// GLOBAL: TOY2 0x004EC948
		ParticlePreset g_particlePresets[29] = {
			{ 0x00A00032, 0x8020000B, 0x00180C07, 0x00320001, 0x00B00032, 0x000B, (int16_t)0x8020 },
			{ 0x00001010, 0x0000003F, 0x00000000, 0x0000003F, 0xFFFFFFF0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0, 0 },
			{ 0x00001010, 0x0000001F, 0xFFFFFE00, 0x0000001F, 0, 0, 0 },
			{ 0x00001510, 0x0000007F, 0x0000087F, 0x0000007F, 0x00000040, 0, 0 },
			{ 0, 0, 0, 0, 0, 0x0200, -48 },
			{ 0, 0, 0, 0, 0, 0, -12 },
			{ 0x00001510, 0x0000007F, 0x000010FF, 0x0000007F, 0x00000040, 0, 0 },
			{ 0x00001010, 0x000000FF, 0xFFFFF800, 0x000000FF, 0x00000080, 0, 0 },
			{ 0x00001510, 0x0000007F, 0x00000E7F, 0x0000007F, 0x00000080, 0, 0 },
			{ 0x00001010, 0x0000000F, 0xFFFFFE00, 0x0000000F, 0xFFFFFFF0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0, -128 },
			{ 0, 0, 0, 0, 0, 0, -192 },
			{ 0x00003510, 0x000000FF, 0x00000E7F, 0x0000007F, 0x00000080, 0, 0 },
			{ 0x00001510, 0x000000FF, 0x000016FF, 0x000000FF, 0x00000080, 0, 0 },
			{ 0x00001110, 0x0000001F, 0x0000001F, 0x0000001F, 0, 0, 0 },
			{ 0, 0, 0xFFFFFE00, 0, 0x00000020, 0, 0 },
			{ 0x00001510, 0x000000FF, 0x00000E7F, 0x000000FF, 0x00000080, 0, 0 },
			{ 0, 0, 0xFFFFFF00, 0, 0xFFFFFFF8, 0, 0 },
			{ 0, 0, 0, 0, 0x000000C0, 0, 0 },
			{ 0x00001010, 0x0000003F, 0xFFFFFF00, 0x0000003F, 0xFFFFFFF0, 0, 0 },
			{ 0x00001010, 0x0000001F, 0xFFFFFB00, 0x0000001F, 0xFFFFFFC0, 0, 0 },
			{ 0x00000110, 0xFFFFF600, 0x0000001F, 0x0000001F, 0xFFFFFFF8, 0, 0 },
			{ 0x00000110, 0x00000A00, 0x0000001F, 0x0000001F, 0xFFFFFFF8, 0, 0 },
			{ 0, 0, 0xFFFFF400, 0xFFFFFA00, 0x00000100, 0, 0 },
			{ 0, 0x00001000, 0xFFFFF800, 0, 0x000000A0, 0, 0 },
			{ 0x00001010, 0x0000001F, 0xFFFFFE00, 0x0000001F, 0xFFFFFFF0, 0, 0 },
			{ 0x00001010, 0x0000001F, 0xFFFFFE00, 0x0000001F, 0x00000010, 0, 0 },
			{ 0, 0, 0x00000140, 0, 0, 0, 0 },
		};

		// GLOBAL: TOY2 0x004EC170
		ParticleType g_particleTypes[124] = {
			{ 4, 100, 16, 1, 0x00, 100, 100, 0x0020, 4, 0x80, 0x20, 0x00 },
			{ 1, 2, 16, 8, 0x00, 100, 100, 0x0020, 2, 0x30, 0x20, 0x10 },
			{ 1, 2, 16, 8, 0x00, 50, 50, 0x0040, 2, 0x40, 0x60, 0x80 },
			{ 6, 100, 8, 1, 0x00, 10, 10, 0x0020, 5, 0xFF, 0x80, 0x00 },
			{ 4, 120, 128, 1, 0x00, 30, 30, 0x00C0, 6, 0x20, 0x30, 0x40 },
			{ 5, 100, 200, 1, 0x00, 200, 200, 0x0020, 2, 0x60, 0x20, 0x00 },
			{ 5, 100, 200, 1, 0x00, 150, 150, 0x0020, 2, 0x60, 0x60, 0x40 },
			{ 8, 100, 200, 1, 0x00, 150, 150, 0x01A0, 0, 0x00, 0xFF, 0x00 },
			{ 5, 100, 200, 1, 0x00, 75, 75, 0x01A0, 0, 0x80, 0x00, 0x00 },
			{ 5, 100, 16, 1, 0x00, 200, 200, 0x0020, 7, 0x80, 0x80, 0x00 },
			{ 5, 100, 16, 1, 0x00, 200, 200, 0x0020, 7, 0x80, 0x20, 0x00 },
			{ 21, 100, 200, 1, 0x01, 150, 150, 0x01A3, 8, 0x00, 0x40, 0x80 },
			{ 24, 2, 12, 6, 0x00, 70, 70, 0x0020, 2, 0x00, 0x40, 0x80 },
			{ 22, 94, 50, 1, 0x00, 250, 250, 0x00B2, 2, 0x00, 0x40, 0x80 },
			{ 19, 100, 24, 1, 0x00, 110, 110, 0x0020, 9, 0x30, 0x38, 0x40 },
			{ 1, 2, 16, 8, 0x00, 200, 200, 0x0020, 10, 0x40, 0x20, 0xFF },
			{ 19, 100, 32, 1, 0x00, 110, 110, 0x0020, 9, 0x40, 0x50, 0x60 },
			{ 17, 6, 16, 1, 0x00, 110, 110, 0x0030, 11, 0x40, 0x00, 0x60 },
			{ 17, 5, 18, 1, 0x00, 10, 10, 0x0030, 11, 0x10, 0x60, 0x20 },
			{ 17, 100, 12, 1, 0x00, 270, 270, 0x0030, 12, 0x20, 0x80, 0x30 },
			{ 17, 100, 12, 1, 0x00, 270, 270, 0x0030, 12, 0x80, 0x00, 0xAA },
			{ 17, 100, 8, 1, 0x00, 270, 270, 0x0030, 13, 0x30, 0xC0, 0x48 },
			{ 17, 100, 8, 1, 0x00, 270, 270, 0x0030, 13, 0xC0, 0x00, 0xFF },
			{ 2, 4, 16, 4, 0x00, 100, 50, 0x0020, 2, 0x80, 0x48, 0x20 },
			{ 0, 100, 16, 1, 0x00, 256, 256, 0x0182, 0, 0xFF, 0x80, 0x00 },
			{ 5, 100, 24, 1, 0x00, 200, 200, 0x01B0, 14, 0x80, 0x40, 0x00 },
			{ 7, 2, 16, 1, 0x00, 70, 70, 0x0030, 11, 0x00, 0x18, 0x18 },
			{ 7, 4, 16, 1, 0x00, 70, 70, 0x0030, 11, 0x00, 0x30, 0x30 },
			{ 24, 2, 12, 6, 0x00, 70, 70, 0x0000, 0, 0x30, 0x20, 0x40 },
			{ 24, 2, 12, 6, 0x00, 70, 70, 0x0000, 0, 0x00, 0x80, 0x20 },
			{ 18, 100, 32, 1, 0x00, 60, 30, 0x0030, 2, 0x00, 0x40, 0x00 },
			{ 18, 100, 32, 1, 0x00, 60, 30, 0x0030, 2, 0x00, 0x40, 0x40 },
			{ 18, 100, 32, 1, 0x00, 60, 30, 0x0050, 2, 0x40, 0x40, 0x40 },
			{ 0, 100, 60, 1, 0x00, 180, 180, 0x0182, 0, 0xFF, 0x80, 0x00 },
			{ 5, 100, 60, 1, 0x03, 180, 180, 0x00A1, 16, 0xFF, 0x80, 0x00 },
			{ 19, 100, 16, 1, 0x00, 120, 120, 0x0020, 2, 0xC0, 0x40, 0x00 },
			{ 19, 100, 16, 1, 0x00, 200, 200, 0x0020, 5, 0xC0, 0xC0, 0x00 },
			{ 5, 100, 60, 1, 0x03, 120, 120, 0x01A3, 17, 0x00, 0x20, 0xFF },
			{ 7, 1, 16, 1, 0x00, 80, 80, 0x0020, 15, 0x00, 0x20, 0x80 },
			{ 7, 8, 16, 1, 0x00, 50, 50, 0x0030, 46, 0x00, 0x20, 0x80 },
			{ 5, 100, 16, 1, 0x00, 100, 100, 0x0020, 2, 0xFF, 0x80, 0x00 },
			{ 19, 100, 16, 1, 0x00, 110, 110, 0x0020, 9, 0x50, 0x40, 0x30 },
			{ 5, 100, 60, 1, 0x00, 80, 80, 0x00A0, 18, 0x80, 0x60, 0x00 },
			{ 5, 5, 20, 1, 0x00, 50, 50, 0x0020, 5, 0xC0, 0x80, 0x20 },
			{ 21, 100, 200, 1, 0xE5, 50, 50, 0x0020, 19, 0x20, 0x30, 0x40 },
			{ 19, 2, 16, 1, 0x00, 60, 60, 0x0020, 20, 0xC0, 0xC0, 0x00 },
			{ 5, 100, 100, 1, 0x00, 110, 110, 0x01A0, 21, 0x80, 0x80, 0x00 },
			{ 23, 60, 200, 1, 0x00, 8, 8, 0x01A0, 22, 0x00, 0xFF, 0x00 },
			{ 17, 100, 64, 1, 0x00, 500, 500, 0x00D0, 2, 0x20, 0x20, 0x20 },
			{ 6, 100, 24, 1, 0x00, 10, 60, 0x0060, 0, 0x00, 0x80, 0x00 },
			{ 7, 1, 48, 1, 0x00, 70, 70, 0x0030, 11, 0x18, 0x16, 0x14 },
			{ 7, 1, 24, 1, 0x00, 70, 70, 0x0030, 11, 0x30, 0x2C, 0x28 },
			{ 24, 2, 12, 6, 0x00, 70, 70, 0x0020, 2, 0x60, 0x58, 0x50 },
			{ 18, 100, 32, 1, 0x00, 60, 30, 0x0030, 2, 0x60, 0x58, 0x50 },
			{ 52, 100, 200, 1, 0x02, 150, 150, 0x01E0, 23, 0x80, 0x80, 0x80 },
			{ 22, 90, 80, 1, 0x00, 250, 250, 0x00B2, 2, 0x00, 0x80, 0x20 },
			{ 7, 6, 16, 1, 0x00, 70, 70, 0x0030, 11, 0x00, 0x30, 0x30 },
			{ 19, 100, 24, 1, 0x00, 110, 110, 0x0020, 9, 0x60, 0x50, 0x40 },
			{ 37, 100, 40, 1, 0x00, 150, 150, 0x01E0, 0, 0x80, 0x80, 0x80 },
			{ 6, 2, 12, 1, 0x00, 96, 5, 0x0020, 24, 0x00, 0x40, 0x80 },
			{ 16, 1, 200, 12, 0x00, 70, 70, 0x01E2, 25, 0x80, 0x80, 0x80 },
			{ 24, 2, 12, 6, 0x00, 70, 70, 0x0020, 2, 0x00, 0x40, 0x00 },
			{ 5, 100, 60, 1, 0x06, 250, 250, 0x01AA, 26, 0x00, 0x80, 0x00 },
			{ 21, 1, 24, 1, 0x00, 60, 60, 0x0020, 33, 0x00, 0x80, 0x00 },
			{ 19, 100, 24, 1, 0x00, 110, 110, 0x0020, 9, 0x00, 0x20, 0x00 },
			{ 19, 100, 16, 1, 0x00, 200, 200, 0x0020, 2, 0x40, 0x40, 0x40 },
			{ 52, 100, 200, 1, 0x07, 150, 150, 0x01E2, 28, 0x80, 0x80, 0x80 },
			{ 1, 2, 32, 8, 0x00, 400, 400, 0x0020, 31, 0x60, 0x20, 0x00 },
			{ 0, 100, 60, 1, 0x00, 300, 300, 0x0182, 0, 0xFF, 0x80, 0x00 },
			{ 4, 32, 64, 1, 0x00, 75, 75, 0x00D0, 3, 0x30, 0x30, 0x30 },
			{ 28, 100, 47, 1, 0x00, 75, 40, 0x01E8, 29, 0x80, 0x80, 0x80 },
			{ 28, 100, 40, 1, 0x00, 75, 40, 0x01E0, 30, 0x80, 0x80, 0x80 },
			{ 4, 100, 16, 1, 0x00, 100, 100, 0x0020, 4, 0x40, 0x80, 0x40 },
			{ 5, 100, 16, 1, 0x00, 200, 200, 0x0020, 7, 0x00, 0x80, 0x00 },
			{ 28, 1, 16, 1, 0x00, 80, 40, 0x0020, 37, 0x00, 0x30, 0x00 },
			{ 5, 100, 15, 1, 0x00, 200, 200, 0x01A2, 32, 0x60, 0x70, 0x80 },
			{ 5, 4, 12, 1, 0x00, 150, 150, 0x0020, 33, 0x60, 0x70, 0x80 },
			{ 17, 2, 20, 1, 0x00, 550, 550, 0x0020, 34, 0x30, 0x30, 0x30 },
			{ 17, 4, 20, 1, 0x00, 550, 550, 0x0030, 34, 0x30, 0x30, 0x30 },
			{ 51, 100, 210, 1, 0x09, 250, 250, 0x01E2, 35, 0x40, 0x40, 0x40 },
			{ 17, 100, 12, 1, 0x00, 40, 40, 0x0030, 36, 0x20, 0x80, 0x40 },
			{ 17, 100, 12, 1, 0x00, 40, 40, 0x0030, 36, 0x80, 0x00, 0xAA },
			{ 17, 100, 12, 1, 0x00, 40, 40, 0x0030, 36, 0xAA, 0x00, 0x00 },
			{ 52, 99, 100, 1, 0x07, 150, 150, 0x01E2, 28, 0x80, 0x80, 0x80 },
			{ 19, 4, 16, 1, 0x00, 20, 20, 0x0020, 20, 0xC0, 0xC0, 0x00 },
			{ 52, 100, 60, 1, 0xA9, 100, 100, 0x01E2, 38, 0x40, 0x40, 0x40 },
			{ 1, 2, 16, 8, 0x00, 150, 150, 0x0020, 2, 0x40, 0x40, 0x40 },
			{ 21, 100, 32, 1, 0x00, 75, 75, 0x0020, 2, 0x3C, 0x46, 0x50 },
			{ 0, 100, 60, 1, 0x00, 200, 200, 0x01AA, 39, 0xFE, 0x80, 0x00 },
			{ 19, 1, 32, 1, 0x00, 200, 200, 0x0020, 40, 0xC0, 0xC0, 0x00 },
			{ 23, 60, 2, 1, 0x00, 100, 100, 0x0061, 0, 0xFF, 0xFF, 0xFF },
			{ 4, 100, 60, 1, 0x08, 100, 100, 0x01E3, 16, 0x40, 0x40, 0x40 },
			{ 5, 100, 60, 1, 0x00, 200, 200, 0x01A3, 32, 0x60, 0x70, 0x80 },
			{ 5, 100, 16, 1, 0x00, 100, 100, 0x0020, 2, 0x80, 0x80, 0x80 },
			{ 51, 100, 32, 1, 0x00, 100, 100, 0x0060, 0, 0x3C, 0x46, 0x50 },
			{ 52, 100, 60, 1, 0xA9, 100, 100, 0x01E3, 0, 0x80, 0x80, 0x00 },
			{ 4, 100, 80, 1, 0xA9, 30, 30, 0x01E3, 41, 0x40, 0x40, 0x40 },
			{ 1, 2, 16, 8, 0x00, 30, 30, 0x0020, 2, 0x40, 0x40, 0x40 },
			{ 31, 64, 100, 1, 0x00, 100, 100, 0x00E0, 42, 0x80, 0x80, 0x80 },
			{ 19, 100, 16, 1, 0x00, 50, 50, 0x0020, 9, 0x40, 0x50, 0x60 },
			{ 1, 2, 32, 8, 0x00, 250, 250, 0x0020, 31, 0x60, 0x20, 0x00 },
			{ 53, 100, 60, 1, 0x03, 350, 175, 0x01BA, 43, 0x00, 0x1C, 0x70 },
			{ 53, 2, 16, 1, 0x00, 250, 125, 0x0020, 27, 0x00, 0x20, 0x80 },
			{ 54, 100, 40, 1, 0x00, 200, 200, 0x01A2, 44, 0x80, 0x20, 0x00 },
			{ 55, 1, 24, 1, 0x00, 75, 200, 0x0050, 24, 0x40, 0x40, 0x40 },
			{ 52, 1, 24, 1, 0x00, 200, 300, 0x0050, 2, 0x40, 0x40, 0x40 },
			{ 53, 4, 24, 1, 0x00, 400, 200, 0x0030, 45, 0x00, 0x20, 0x80 },
			{ 52, 100, 200, 1, 0xA9, 50, 50, 0x01E2, 47, 0x80, 0x80, 0x00 },
			{ 52, 98, 100, 1, 0xA9, 50, 50, 0x01EA, 48, 0x00, 0x80, 0x00 },
			{ 52, 1, 16, 1, 0x00, 50, 50, 0x0020, 2, 0x20, 0x20, 0x00 },
			{ 52, 1, 16, 1, 0x00, 50, 50, 0x0020, 2, 0x00, 0x20, 0x00 },
			{ 17, 4, 20, 1, 0x00, 550, 550, 0x0030, 34, 0x00, 0x30, 0x00 },
			{ 36, 100, 16, 1, 0x00, 150, 150, 0x0060, 0, 0x80, 0x80, 0x80 },
			{ 5, 98, 100, 1, 0x03, 100, 100, 0x01AA, 49, 0x80, 0x20, 0x00 },
			{ 37, 100, 16, 1, 0x00, 150, 150, 0x0060, 0, 0x80, 0x80, 0x80 },
			{ 7, 100, 40, 1, 0x00, 220, 220, 0x00B2, 50, 0x80, 0x00, 0x00 },
			{ 5, 118, 120, 1, 0x00, 100, 100, 0x01A0, 51, 0x80, 0x40, 0x00 },
			{ 19, 100, 16, 1, 0x00, 50, 50, 0x0020, 2, 0x80, 0x00, 0x00 },
			{ 55, 100, 200, 1, 0x0A, 100, 100, 0x01A3, 52, 0x80, 0x80, 0x80 },
			{ 53, 94, 800, 1, 0x00, 250, 250, 0x01B2, 2, 0x80, 0x80, 0x80 },
			{ 5, 122, 40, 1, 0x0B, 100, 100, 0x01A0, 51, 0x80, 0x80, 0x00 },
			{ 19, 100, 16, 1, 0x00, 75, 75, 0x0040, 2, 0x40, 0x60, 0x80 },
			{ 53, 100, 32, 1, 0x00, 200, 200, 0x01D0, 2, 0x30, 0x30, 0x30 },
			{ 1, 2, 16, 8, 0x00, 200, 200, 0x00A2, 2, 0x40, 0x30, 0x20 },
		};

		// GLOBAL: TOY2 0x00528168
		ParticleInstance g_rejectedParticleInstance;

		// FUNCTION: TOY2 0x0040FAC0 [MATCHED]
		void Init()
		{
			memset(g_particleInstances, 0, sizeof(g_particleInstances));
			g_particleAllocationCursor = 0;
		}

		// FUNCTION: TOY2 0x0040FAE0 [PROVISIONAL]
		ParticleInstance* SpawnInstance(int32_t x,
			int32_t y,
			int32_t z,
			int32_t velocityX,
			int32_t velocityY,
			int32_t velocityZ,
			int32_t yawAngle,
			int32_t groundAlignRotation,
			int32_t rotationSpeed,
			int32_t typeId)
		{
			int32_t maximumDistanceSquared;
			if (typeId == 0x37 || typeId == 0x43 || typeId == 0x50)
			{
				maximumDistanceSquared = 0x190000;
			}
			else
			{
				maximumDistanceSquared = 640000;
			}

			int32_t distanceZ = (Toy2::Camera::g_renderCameraTransform.pos.z - z) >> 8;
			int32_t distanceY = (Toy2::Camera::g_renderCameraTransform.pos.y - y) >> 8;
			int32_t distanceX = (Toy2::Camera::g_renderCameraTransform.pos.x - x) >> 8;
			if (distanceZ * distanceZ + distanceY * distanceY + distanceX * distanceX >= maximumDistanceSquared)
			{
				return &g_rejectedParticleInstance;
			}

			g_particleAllocationCursor++;
			if (g_particleAllocationCursor >= 64)
			{
				g_particleAllocationCursor = 0;
			}

			int32_t oldestParticleIndex = -1;
			int32_t shortestLifetime = 9999;
			int32_t checkedCount = 0;
			while ((g_particleInstances[g_particleAllocationCursor].renderFlags & Renderer::RENDER_TEXTURE_WRAP_UV) != 0
				&& g_particleInstances[g_particleAllocationCursor].lifetime != 0)
			{
				if (g_particleInstances[g_particleAllocationCursor].lifetime < shortestLifetime)
				{
					shortestLifetime = g_particleInstances[g_particleAllocationCursor].lifetime;
					oldestParticleIndex = g_particleAllocationCursor;
				}

				checkedCount++;
				if (checkedCount >= 64)
				{
					g_particleAllocationCursor = oldestParticleIndex;
					break;
				}

				g_particleAllocationCursor++;
				if (g_particleAllocationCursor >= 64)
				{
					g_particleAllocationCursor = 0;
				}
			}

			ParticleInstance* particle = &g_particleInstances[g_particleAllocationCursor];
			const ParticleType* particleType = &g_particleTypes[typeId - 1];
			particle->pos.z = z;
			particle->pos.x = x;
			particle->pos.y = y;
			particle->velX = velocityX / 2;
			particle->groundHeightY = INT_MIN;
			particle->velY = velocityY / 2;
			particle->velZ = velocityZ / 2;
			particle->yawAngle = yawAngle / 4;
			particle->lifetime = particleType->lifetime * 2;
			particle->width = particleType->width;
			particle->height = particleType->height;

			if (particleType->animationRate < 0)
			{
				particle->updateParam = (particleType->animationRate * 127 + (*g_randDatBufferPtr & 3)) * 2;
				g_randDatBufferPtr++;
				particle->lifetime = particle->animationFrameCount * particle->updateParam * 2;
			}
			else
			{
				particle->updateParam = particleType->animationRate * 2;
			}

			particle->animationTimer = particle->updateParam;
			particle->spriteSheet = particleType->spriteSheet;
			particle->typeId = typeId;
			particle->collisionFlags = particleType->collisionFlags;
			particle->updateMode = particleType->updateMode;
			particle->animationFrameCount = particleType->animationFrameCount;
			particle->tileIndex = 0;
			particle->groundAlignRot = groundAlignRotation;
			particle->renderFlags = particleType->renderFlags | Renderer::RENDER_BILINEAR_FILTER;
			particle->rotSpeed = rotationSpeed / 2;
			particle->colourR = particleType->colourR;
			particle->colourG = particleType->colourG;
			particle->colourB = particleType->colourB;
			SetDefaultAlpha(particle);
			return particle;
		}

		// FUNCTION: TOY2 0x0040FDF0 [PROVISIONAL]
		ParticleInstance* SpawnFromPreset(int32_t x, int32_t y, int32_t z, int32_t typeId, int32_t presetIndex)
		{
			const ParticlePreset* preset = &g_particlePresets[presetIndex];
			int32_t velocityX = preset->velocityXMask;
			switch ((preset->randomModes >> 12) & 0xF)
			{
				case 1:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) - (velocityX >> 1)) << 3;
					break;
				case 2:
					velocityX = (*g_randDatBufferPtr++ & velocityX) << 3;
					break;
				case 3:
					velocityX = -(*g_randDatBufferPtr++ & velocityX) << 3;
					break;
				case 4:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) + ((velocityX >> 4) & 0xFF0)) << 3;
					break;
				case 5:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) - ((velocityX >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) - (velocityX >> 1)) << 4;
					break;
			}

			int32_t velocityY = preset->velocityYMask;
			switch ((preset->randomModes >> 8) & 0xF)
			{
				case 1:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) - (velocityY >> 1)) << 3;
					break;
				case 2:
					velocityY = (*g_randDatBufferPtr++ & velocityY) << 3;
					break;
				case 3:
					velocityY = -(*g_randDatBufferPtr++ & velocityY) << 3;
					break;
				case 4:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) + ((velocityY >> 4) & 0xFF0)) << 3;
					break;
				case 5:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) - ((velocityY >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) - (velocityY >> 1)) << 4;
					break;
			}

			int32_t velocityZ = preset->velocityZMask;
			switch ((preset->randomModes >> 4) & 0xF)
			{
				case 1:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) - (velocityZ >> 1)) << 3;
					break;
				case 2:
					velocityZ = (*g_randDatBufferPtr++ & velocityZ) << 3;
					break;
				case 3:
					velocityZ = -(*g_randDatBufferPtr++ & velocityZ) << 3;
					break;
				case 4:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) + ((velocityZ >> 4) & 0xFF0)) << 3;
					break;
				case 5:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) - ((velocityZ >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) - (velocityZ >> 1)) << 4;
					break;
			}

			int32_t yawAngle = preset->yawMask;
			switch (preset->randomModes & 0xF)
			{
				case 1:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) - (yawAngle >> 1)) << 3;
					break;
				case 2:
					yawAngle = (*g_randDatBufferPtr++ & yawAngle) << 3;
					break;
				case 3:
					yawAngle = -(*g_randDatBufferPtr++ & yawAngle) << 3;
					break;
				case 4:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) + ((yawAngle >> 4) & 0xFF0)) << 3;
					break;
				case 5:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) - ((yawAngle >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) - (yawAngle >> 1)) << 4;
					break;
			}

			return SpawnInstance(x, y, z, velocityX, velocityY, velocityZ, yawAngle, preset->groundAlignRotation, preset->rotationSpeed, typeId);
		}

		// GLOBAL: TOY2 0x00529E58;
		ParticleInstance g_particleInstances[64];

		// GLOBAL: TOY2 0x0052AD90
		int32_t g_particleAllocationCursor;

		// FUNCTION: TOY2 0x00446FA0 [MATCHED]
		void SetDefaultAlpha(ParticleInstance* particle)
		{
			particle->colourA = 0x2C;
			if ((particle->renderFlags & PARTICLE_RENDER_ALPHA_MODE_MASK) != PARTICLE_RENDER_ALPHA_MODE_MASK)
			{
				particle->colourA = 0x2E;
			}
		}

		// FUNCTION: TOY2 0x00425AD0 [MATCHED]
		void ReflectWallsSquareArena(ParticleInstance* particle)
		{
			const int32_t westWall = -0x169EB;
			const int32_t eastWall = 0x16915;
			const int32_t northWall = -0x16CEF;
			const int32_t southWall = 0x16991;
			const int32_t wallImpactSound = 0x4A;

			if (particle->pos.x < westWall)
			{
				particle->velX = abs(particle->velX);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
			if (particle->pos.x > eastWall)
			{
				particle->velX = -abs(particle->velX);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
			if (particle->pos.z < northWall)
			{
				particle->velZ = abs(particle->velZ);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
			if (particle->pos.z > southWall)
			{
				particle->velZ = -abs(particle->velZ);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
		}

		// FUNCTION: TOY2 0x0042B090 [PROVISIONAL]
		void UpdateArenaBounce(ParticleInstance* particle)
		{
			const int32_t drainCenterX = -0xBD7C;
			const int32_t drainCenterZ = 0x99;
			int32_t bounced = 0;

			if (particle->lifetime > 4 && Toy2::g_fourTickPulse != 0)
			{
				SpawnFromPreset(particle->pos.x - particle->velX * Renderer::g_frameDelta,
					particle->pos.y,
					particle->pos.z - particle->velZ * Renderer::g_frameDelta,
					0x6E,
					2);
			}

			if (Toy2::Actor::IsInsideBounds(&particle->pos, -0x256D6, 0xE0AA, -0x1B3E9, 0x1B297))
			{
				const int32_t arenaFloorY = -0x12BD3;
				if (particle->groundHeightY == INT_MIN)
				{
					particle->groundHeightY = arenaFloorY;
				}

				if (particle->pos.y > arenaFloorY)
				{
					if (particle->groundHeightY == arenaFloorY)
					{
						particle->pos.y = arenaFloorY;
						bounced = 1;
						particle->velY = -abs((particle->velY * 7) >> 3);
					}
					else
					{
						particle->velX = -particle->velX;
						particle->velZ = -particle->velZ;
						bounced = 1;
					}
				}
				else
				{
					particle->groundHeightY = arenaFloorY;
					if (Renderer::Shadows::g_shadowCount < 47)
					{
						int16_t shadowIndex = Renderer::Shadows::g_shadowCount;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.x = particle->pos.x;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.y = particle->groundHeightY;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.z = particle->pos.z;
						Renderer::Shadows::g_shadowInstances[shadowIndex].size = particle->width;
						Renderer::Shadows::g_shadowCount = shadowIndex + 1;
					}
				}
			}
			else
			{
				particle->groundHeightY = 400000;
			}

			int32_t posX = particle->pos.x;
			if (posX < -0x29B56)
			{
				bounced = 1;
				particle->velX = abs(particle->velX);
			}
			if (posX > 0x2A62A)
			{
				bounced = 1;
				particle->velX = -abs(particle->velX);
			}

			int32_t posZ = particle->pos.z;
			if (posZ < -0x230E9)
			{
				bounced = 1;
				particle->velZ = abs(particle->velZ);
			}
			if (posZ > 0x22B97)
			{
				bounced = 1;
				particle->velZ = -abs(particle->velZ);
			}

			int32_t deltaX = (drainCenterX - posX) >> 5;
			int32_t deltaZ = (drainCenterZ - posZ) >> 5;
			if (deltaZ * deltaZ + deltaX * deltaX < 0x100000)
			{
				particle->lifetime = 0;
			}

			if (bounced)
			{
				AudioManager::PlaySoundEffect(0x43, &particle->pos);
			}
		}

		// FUNCTION: TOY2 0x0042B250 [MATCHED]
		void UpdateDrainTrail(ParticleInstance* particle)
		{
			const int32_t drainCenterX = -0xBD7C;
			const int32_t drainCenterZ = 0x99;
			const int32_t triggerRadius = 0x8000;
			int32_t deltaX = (drainCenterX - particle->pos.x) >> 5;
			int32_t deltaZ = (drainCenterZ - particle->pos.z) >> 5;

			if (deltaZ * deltaZ + deltaX * deltaX < (triggerRadius >> 5) * (triggerRadius >> 5))
			{
				particle->lifetime = 0;
			}

			if (particle->lifetime > 4 && Toy2::g_fourTickPulse != 0)
			{
				SpawnFromPreset(particle->pos.x - particle->velX * Renderer::g_frameDelta,
					particle->pos.y,
					particle->pos.z - particle->velZ * Renderer::g_frameDelta,
					0x6F,
					2);
			}
		}
	}
}
