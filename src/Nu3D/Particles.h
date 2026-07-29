#pragma once

#include "Numerics.h"

#include <stddef.h>

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
			PARTICLE_INTERACTS_WITH_BUZZ = 0x2,
			PARTICLE_RENDER_ALPHA_MODE_MASK = 0x60,
		};

		struct ParticleType
		{
			uint8_t spriteSheet;
			int8_t animationRate;
			int16_t lifetime;
			uint8_t animationFrameCount;
			uint8_t collisionFlags;
			int16_t width;
			int16_t height;
			int16_t renderFlags;
			uint8_t updateMode;
			uint8_t colourR;
			uint8_t colourG;
			uint8_t colourB;
		};

		struct ParticlePreset
		{
			int32_t randomModes;
			int32_t velocityXMask;
			int32_t velocityYMask;
			int32_t velocityZMask;
			int32_t yawMask;
			int16_t groundAlignRotation;
			int16_t rotationSpeed;
		};

		struct ParticleInstance
		{
			union
			{
				PosAndAngles groundProbe;
				struct
				{
					Vector3I pos;
					int32_t velX;
				};
			};
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
			uint8_t animationTimer;
			uint8_t spriteSheet;
			uint8_t typeId;
			int8_t collisionFlags;
			uint8_t updateMode;
			uint8_t animationFrameCount;
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
		extern ParticleInstance g_rejectedParticleInstance;
		extern ParticleType g_particleTypes[124];
		extern ParticlePreset g_particlePresets[29];
		extern int32_t g_particleAllocationCursor;
		void Init();
		void SetDefaultAlpha(ParticleInstance* particle);
		void ReflectWallsSquareArena(ParticleInstance* particle);
		void UpdateArenaBounce(ParticleInstance* particle);
		void UpdateDrainTrail(ParticleInstance* particle);
		void Update();
		ParticleInstance* SpawnInstance(int32_t x,
			int32_t y,
			int32_t z,
			int32_t velocityX,
			int32_t velocityY,
			int32_t velocityZ,
			int32_t yawAngle,
			int32_t groundAlignRotation,
			int32_t rotationSpeed,
			int32_t typeId);
		ParticleInstance* SpawnFromPreset(int32_t x, int32_t y, int32_t z, int32_t typeId, int32_t presetIndex);

		STATIC_ASSERT(sizeof(ParticleInstance) == 0x3C);
		STATIC_ASSERT(offsetof(ParticleInstance, groundProbe) == 0x0);
		STATIC_ASSERT(offsetof(ParticleInstance, pos) == 0x0);
		STATIC_ASSERT(offsetof(ParticleInstance, velX) == 0xC);
		STATIC_ASSERT(offsetof(ParticleInstance, lifetime) == 0x24);
		STATIC_ASSERT(offsetof(ParticleInstance, width) == 0x26);
		STATIC_ASSERT(offsetof(ParticleInstance, updateParam) == 0x2A);
		STATIC_ASSERT(offsetof(ParticleInstance, spriteSheet) == 0x2C);
		STATIC_ASSERT(offsetof(ParticleInstance, collisionFlags) == 0x2E);
		STATIC_ASSERT(offsetof(ParticleInstance, renderFlags) == 0x32);
		STATIC_ASSERT(sizeof(ParticleType) == 0x10);
		STATIC_ASSERT(sizeof(ParticlePreset) == 0x18);
	}
}
