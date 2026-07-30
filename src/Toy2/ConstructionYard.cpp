#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <math.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace ConstructionYard
	{
		enum DrillEncounterState
		{
			DRILL_ENCOUNTER_ACTIVE = 2,
			DRILL_ENCOUNTER_DEFEATED = 3,
		};

		// GLOBAL: TOY2 0x004F1CCC
		int32_t g_drillArenaXThresholds[4] = { -336724, -158292, -129428, 49196 };
		// GLOBAL: TOY2 0x004F1CDC
		int32_t g_drillArenaZThresholds[4] = { -496094, -317790, -289694, -111262 };
		// GLOBAL: TOY2 0x004F1CEC
		int32_t g_drillArenaXEdges[4] = { -336724, -158291, -129428, 49197 };
		// GLOBAL: TOY2 0x004F1CFC
		int32_t g_drillArenaZEdges[4] = { -496094, -317789, -289694, -111261 };

		// GLOBAL: TOY2 0x0052F798
		int32_t g_drillTintToggle;
		// GLOBAL: TOY2 0x0052F7CC
		int32_t g_drillArenaRegionCode;
		// GLOBAL: TOY2 0x0052F804
		int32_t g_drillEncounterState;
		// GLOBAL: TOY2 0x0052F864
		int32_t g_drillTintTimer;
		// GLOBAL: TOY2 0x0052F884
		int32_t g_previousDrillPhase;

		// STUB: TOY2 0x0041C190
		void Init() {}

		// STUB: TOY2 0x0041C640
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0041B780 [PROVISIONAL]
		void Drill(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			ConstructionYard::g_drillTintToggle = (ConstructionYard::g_drillTintToggle - 1) & 1;
			AudioManager::PlaySoundEffect(0x99, &actor->pos);

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				actor->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				AudioManager::PlaySoundEffect(0x9A, &actor->pos);
			}

			if (Camera::g_shakeTimer == 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 300) != 0)
			{
				Camera::g_shakeTimer = 20;
			}
			else if (Camera::g_shakeTimer < 20 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 100) != 0)
			{
				Camera::g_shakeTimer = 40;
			}

			if (ConstructionYard::g_drillEncounterState > 1)
			{
				ConstructionYard::g_drillTintTimer -= Renderer::g_frameDelta;
				if (ConstructionYard::g_drillTintTimer < 0)
				{
					ConstructionYard::g_drillTintTimer = 0;
					actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (ConstructionYard::g_drillTintToggle != 0)
				{
					actor->useTint = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
				else
				{
					actor->useTint = 0;
				}
			}

			if (actor->actorPhase != ConstructionYard::g_previousDrillPhase)
			{
				ConstructionYard::g_previousDrillPhase = actor->actorPhase;
				ConstructionYard::g_drillTintTimer = 60;
				actor->creatureRam->defenseMode = 4;
			}

			if (g_discLauncherShotSlotsAvailable == 6 || ConstructionYard::g_drillEncounterState <= 1)
				actor->creatureRam->defenseMode = 4;
			else
				actor->creatureRam->defenseMode = 5;

			int32_t arenaRegionCode = 0;
			int32_t* arenaThreshold = ConstructionYard::g_drillArenaXThresholds;
			for (;;)
			{
				if (actor->pos.x < *arenaThreshold)
					break;
				arenaThreshold++;
				arenaRegionCode++;
				if (arenaThreshold >= ConstructionYard::g_drillArenaXThresholds + 4)
					break;
			}

			arenaThreshold = ConstructionYard::g_drillArenaZThresholds;
			for (;;)
			{
				if (actor->pos.z < *arenaThreshold)
					break;
				arenaThreshold++;
				arenaRegionCode += 0x10;
				if (arenaThreshold >= ConstructionYard::g_drillArenaZThresholds + 4)
					break;
			}

			if ((arenaRegionCode & 0x11) == 0x11)
			{
				if ((ConstructionYard::g_drillArenaRegionCode & 1) == 0)
				{
					int32_t regionIndex = (arenaRegionCode & 3) - 1;
					int32_t lowerEdge = ConstructionYard::g_drillArenaXEdges[regionIndex];
					int32_t upperEdge = ConstructionYard::g_drillArenaXEdges[regionIndex + 1];
					if (actor->pos.x < upperEdge - (upperEdge - lowerEdge) / 2)
						actor->pos.x = lowerEdge;
					else
						actor->pos.x = upperEdge;
					arenaRegionCode &= ~1;
				}

				if ((ConstructionYard::g_drillArenaRegionCode & 0x10) == 0)
				{
					int32_t regionIndex = ((arenaRegionCode >> 4) & 3) - 1;
					int32_t lowerEdge = ConstructionYard::g_drillArenaZEdges[regionIndex];
					int32_t upperEdge = ConstructionYard::g_drillArenaZEdges[regionIndex + 1];
					if (actor->pos.z < upperEdge - (upperEdge - lowerEdge) / 2)
						actor->pos.z = lowerEdge;
					else
						actor->pos.z = upperEdge;
					arenaRegionCode &= ~0x10;
				}
			}
			ConstructionYard::g_drillArenaRegionCode = arenaRegionCode;

			if ((uint32_t)actor->pos.y > (uint32_t)-639968)
				actor->pos.y = -639968;

			if (ConstructionYard::g_drillEncounterState < ConstructionYard::DRILL_ENCOUNTER_DEFEATED && g_framePulseOutputs.fourTick != 0)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(actor->pos.x, actor->pos.y, actor->pos.z, 4, 9);
				particle->lifetime = 32;
			}

			if (ConstructionYard::g_drillEncounterState == ConstructionYard::DRILL_ENCOUNTER_ACTIVE && g_buzzActor.posAngles.pos.y < -515621)
			{
				if (g_framePulseOutputs.thirtyTwoTick != 0)
				{
					int32_t particleX = actor->pos.x;
					int32_t particleY = actor->pos.y;
					int32_t particleZ = actor->pos.z;
					Nu3D::Particles::ParticleInstance* particle;
					if ((*g_randDatBufferPtr++ & 3) != 0)
					{
						particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 0x54, 0xE);
					}
					else
					{
						int32_t deltaY = particleY - g_buzzActor.posAngles.pos.y;
						int32_t deltaX = (particleX - g_buzzActor.posAngles.pos.x) >> 5;
						int32_t deltaZ = (particleZ - g_buzzActor.posAngles.pos.z) >> 5;
						int32_t projectileAngle = Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF;
						int32_t horizontalDistance = (int32_t)sqrt((float)(deltaX * deltaX + deltaZ * deltaZ));
						int32_t horizontalSpeed = horizontalDistance * 3 / 4;
						int32_t trajectoryScale = (horizontalDistance << 12) / horizontalSpeed;
						particle = Nu3D::Particles::SpawnInstance(particleX,
							particleY,
							particleZ,
							Numerics::g_sinCosLUT[(projectileAngle - 0x800) & 0xFFF] * horizontalSpeed / 0x4000,
							(-trajectoryScale * 0x80) / 0x100 - (deltaY * 0x80) / trajectoryScale,
							Numerics::g_sinCosLUT[(projectileAngle - 0x400) & 0xFFF] * horizontalSpeed / 0x4000,
							0x80,
							0,
							0,
							0x54);
					}
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					particle->width = 100;
					particle->height = 100;
				}

				g_hudActorAnimationFrame = actor->actorPhase * 54 / 30;
			}
		}

		// FUNCTION: TOY2 0x0041BB80 [MATCHED]
		void LTyke(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x5A;
				AudioManager::PlaySoundEffect(0x6B, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x6C, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}
