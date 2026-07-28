#pragma once

#include "Numerics.h"

namespace Toy2
{
	namespace Actor
	{
		struct Toy2Actor;
	}
}

namespace Nu3D
{
	namespace Particles
	{
		enum RenderFlags
		{
			PARTICLE_RENDER_ALPHA_MODE_MASK = 0x60,
		};

		struct ParticleInstance
		{
			Vector3I pos;
			int32_t velX;
			union
			{
				int32_t velY;
				Toy2::Actor::Toy2Actor* targetActor;
			};
			int32_t velZ;
			int32_t yawAngle;
			int32_t groundHeightY;
			union
			{
				int32_t pitchAngle;
				int32_t discPitchAngle;
			};
			int16_t lifetime;
			int16_t width;
			int16_t height;
			uint8_t updateParam;
			uint8_t unkByte2;
			uint8_t spriteSheet;
			uint8_t typeId;
			uint8_t collisionFlags;
			uint8_t updateMode;
			uint8_t unkByte7;
			uint8_t tileIndex;
			int16_t renderFlags;
			int16_t groundAlignRot;
			int16_t rotSpeed;
			uint8_t colourR;
			uint8_t colourG;
			uint8_t colourB;
			uint8_t colourA;
		};

		extern ParticleInstance g_particleInstances[64];
		extern int32_t g_particleAllocationCursor;
		void Init();
		void SetDefaultAlpha(ParticleInstance* particle);
		void ReflectWallsSquareArena(ParticleInstance* particle);
		void UpdateArenaBounce(ParticleInstance* particle);
		void UpdateDrainTrail(ParticleInstance* particle);
		ParticleInstance* SpawnInstance(int32_t x,
			int32_t y,
			int32_t z,
			int32_t velocityX,
			int32_t velocityY,
			int32_t velocityZ,
			int32_t yawAngle,
			int16_t groundAlignRotation,
			int32_t rotationSpeed,
			int32_t typeId);
		ParticleInstance* SpawnFromPreset(int32_t x, int32_t y, int32_t z, int32_t typeId, int32_t presetIndex);

		STATIC_ASSERT(sizeof(ParticleInstance) == 0x3C);
	}
}
