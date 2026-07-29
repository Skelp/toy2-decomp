#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Collision.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "RawLoader.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Path
	{
		// FUNCTION: TOY2 0x0042C200 [PROVISIONAL]
		void SamplePoint(int32_t pathRecordType, int32_t pathPosition, Vector4I* position)
		{
			int32_t pointIndex = pathPosition / 0x1000;
			int32_t fraction = pathPosition & 0xFFF;

			position->x =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].x - Levels::g_recordData[pathRecordType]->data[pointIndex].x) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].x;
			position->y =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].y - Levels::g_recordData[pathRecordType]->data[pointIndex].y) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].y;
			position->z =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].z - Levels::g_recordData[pathRecordType]->data[pointIndex].z) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].z;
		}
	}

	namespace Platform
	{
		struct PathPlatformState
		{
			int32_t pathPosition;
			int32_t speed;
			int32_t previousX;
			int32_t previousZ;
			int32_t platformIndex;
			int32_t pathRecordType;
			int16_t facingAngle;
			int16_t emitParticles;
			int16_t primaryLinkIndex;
			int16_t secondaryLinkIndex;
		};

		// GLOBAL: TOY2 0x0052FE48
		PathPlatformState g_pathPlatforms[5];

		STATIC_ASSERT(sizeof(PathPlatformState) == 0x20);
		STATIC_ASSERT(offsetof(PathPlatformState, platformIndex) == 0x10);
		STATIC_ASSERT(offsetof(PathPlatformState, facingAngle) == 0x18);
		STATIC_ASSERT(offsetof(PathPlatformState, primaryLinkIndex) == 0x1C);

		// FUNCTION: TOY2 0x0042C2B0 [MATCHED]
		void InitPathPlatform(int32_t pathIndex,
			int32_t platformIndex,
			int32_t pathRecordType,
			int32_t primaryLinkIndex,
			int32_t secondaryLinkIndex,
			int32_t speedLimit,
			int32_t pathPosition,
			int32_t facingAngle)
		{
			g_pathPlatforms[pathIndex].pathPosition = pathPosition;
			g_pathPlatforms[pathIndex].speed = 0;
			g_pathPlatforms[pathIndex].platformIndex = platformIndex;
			g_pathPlatforms[pathIndex].pathRecordType = pathRecordType;
			g_pathPlatforms[pathIndex].primaryLinkIndex = (int16_t)primaryLinkIndex;
			g_pathPlatforms[pathIndex].secondaryLinkIndex = (int16_t)secondaryLinkIndex;
			g_pathPlatforms[pathIndex].facingAngle = (int16_t)facingAngle;
			g_pathPlatforms[pathIndex].emitParticles = 1;
			g_pathPlatforms[pathIndex].previousX = INT_MAX;
			g_pathPlatforms[pathIndex].previousZ = INT_MAX;

			Vector4I position;
			Path::SamplePoint(g_pathPlatforms[pathIndex].pathRecordType, g_pathPlatforms[pathIndex].pathPosition, &position);
			SetOrigin(g_pathPlatforms[pathIndex].platformIndex, position.x << 5, position.y << 5, position.z << 5);
			if (g_pathPlatforms[pathIndex].secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetPositionRawAndCommit(g_pathPlatforms[pathIndex].secondaryLinkIndex, position.x, position.y, position.z);
			}
			Nu3D::Link::SetPositionRawAndCommit(g_pathPlatforms[pathIndex].primaryLinkIndex, position.x, position.y, position.z);
			SetRotationAngles(platformIndex, 0, (int16_t)facingAngle, 0);
			Nu3D::Link::SetRotationRelative8bit(primaryLinkIndex, 0, facingAngle, 0);
			if (secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetRotationRelative8bit(secondaryLinkIndex, 0, facingAngle, 0);
			}
		}
	}

	namespace AirportInfiltration
	{
		enum ProspectorState
		{
			PROSPECTOR_STATE_ACTIVE = 2,
			PROSPECTOR_STATE_DEFEATED = 3,
		};

		// GLOBAL: TOY2 0x004F4668
		Vector3I g_fanParticleVelocities[5] = {
			{ 0, 0, 0x500 },
			{ 0x500, 0, 0 },
			{ 0, 0, -0x500 },
			{ -0x500, 0, 0 },
			{ 0, 0, -0x500 },
		};

		// GLOBAL: TOY2 0x004F46A4
		MoveableObject::InitEntry g_moveableObjectInitTable[] = {
			{ 8, 11, 0 },
			{ -1, 0, 0 },
		};

		// GLOBAL: TOY2 0x004F46C0
		int16_t g_tokenLinkIds[5] = { 49, 50, 52, 48, 51 };

		// GLOBAL: TOY2 0x0052FE38
		int32_t g_slammedPlatformRotation;
		// GLOBAL: TOY2 0x0052FE40
		int32_t g_hiddenCollectiblesVisible;
		// GLOBAL: TOY2 0x0052FEE8
		int32_t g_prospectorEffectTimer;

		struct HiddenCollectibleState
		{
			int32_t* verticalPosition;
			int32_t savedVerticalPosition;
		};

		struct State
		{
			HiddenCollectibleState hiddenCollectibles[5];
			int32_t prospectorTurnAngle;
			int32_t fanBlend;
			int32_t prospectorState;
			int32_t platform3Rotation;
			int32_t platform4Rotation;
			int32_t fanPhase;
			int32_t prospectorTintToggle;
			int32_t prospectorAttackTimer;
			int32_t oddFanRotation;
			int32_t evenFanRotation;
			int32_t previousProspectorPhase;
			int32_t prospectorTargetAngle;
			int32_t prospectorPhaseTimer;
		};

		// GLOBAL: TOY2 0x0052FEEC
		State g_state;

		STATIC_ASSERT(sizeof(HiddenCollectibleState) == 0x8);
		STATIC_ASSERT(sizeof(State) == 0x5C);
		STATIC_ASSERT(offsetof(State, prospectorTurnAngle) == 0x28);
		STATIC_ASSERT(offsetof(State, prospectorState) == 0x30);
		STATIC_ASSERT(offsetof(State, previousProspectorPhase) == 0x50);
		STATIC_ASSERT(offsetof(State, prospectorPhaseTimer) == 0x58);

		// FUNCTION: TOY2 0x0042C810 [PROVISIONAL]
		void SpawnFanParticle(const Vector3I* position, int32_t fanIndex)
		{
			if ((fanIndex == 4 ? g_framePulseOutputs.sevenTick : g_framePulseOutputs.sixteenTick) != 0)
			{
				Nu3D::Particles::ParticleInstance* particle =
					Nu3D::Particles::SpawnFromPreset(position->x, position->y, position->z, fanIndex == 4 ? 0x4F : 0x4E, 2);
				particle->velX = g_fanParticleVelocities[fanIndex].x;
				particle->velY = g_fanParticleVelocities[fanIndex].y;
				particle->velZ = g_fanParticleVelocities[fanIndex].z;
				particle->rotSpeed = -0x100;
				particle->groundAlignRot = 0xFFF - g_framePulsePhases.thirtyTwoTick * 0x40;
			}
		}

		// FUNCTION: TOY2 0x0042C8A0 [MATCHED]
		void InitHiddenCollectibles()
		{
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 56)
					g_state.hiddenCollectibles[0].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 57)
					g_state.hiddenCollectibles[1].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 58)
					g_state.hiddenCollectibles[2].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 59)
					g_state.hiddenCollectibles[3].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 60)
					g_state.hiddenCollectibles[4].verticalPosition = &pickup->position.y;
			}

			for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
			{
				g_state.hiddenCollectibles[hiddenIndex].savedVerticalPosition = *g_state.hiddenCollectibles[hiddenIndex].verticalPosition;
				*g_state.hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
				Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 56, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0042C930 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(g_moveableObjectInitTable);
			Collectables::Init(g_tokenLinkIds, 0x40);
			Collectables::Activate(3, 1);
			InitHiddenCollectibles();

			g_state.platform4Rotation = 0;
			g_state.platform3Rotation = 0;
			Platform::SetRotationAngles(4, 0, -0x400, 0);
			g_state.oddFanRotation = 0;
			g_state.evenFanRotation = 0;
			g_state.prospectorTargetAngle = -0x200;
			g_state.prospectorTurnAngle = 0x200;
			g_slammedPlatformRotation = 0;
			g_state.prospectorAttackTimer = 200;

			Platform::InitPathPlatform(0, 8, 2, 21, 20, 125, 0, 0x400);
			Platform::InitPathPlatform(1, 10, 4, 25, 24, 125, 0, 0x400);
			Platform::InitPathPlatform(2, 2, 6, 13, 12, 125, 0x8000, -0x400);
			Platform::InitPathPlatform(3, 5, 7, 15, 14, 125, 0, -0xC00);
			Platform::InitPathPlatform(4, 1, 5, 11, 10, 125, 0, -0x638);

			int32_t previousProspectorPhase = Actor::g_creatureActors[32].actorPhase;
			RawLoader::CreatureListRam* prospectorRam = Actor::g_creatureActors[32].creatureRam;
			g_hiddenCollectiblesVisible = 1;
			g_state.prospectorState = 0;
			g_state.prospectorPhaseTimer = 0;
			g_state.prospectorTintToggle = 0;
			g_prospectorEffectTimer = 0;
			g_state.previousProspectorPhase = previousProspectorPhase;
			prospectorRam->boundHalfX = 90;
		}

		// STUB: TOY2 0x0042CA60
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0042BE60 [PROVISIONAL]
		void ProsPLevel13(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AirportInfiltration::g_state.prospectorTintToggle = (AirportInfiltration::g_state.prospectorTintToggle - 1) & 1;

			if (actor->actorPhase != AirportInfiltration::g_state.previousProspectorPhase)
			{
				AirportInfiltration::g_state.previousProspectorPhase = actor->actorPhase;
				AirportInfiltration::g_state.prospectorPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;

				int32_t soundIndex = *g_randDatBufferPtr++ & 3;
				if (soundIndex == 3)
					soundIndex = 0;
				AudioManager::Preset::PlayOneShotSound2(soundIndex + 0xC4, actor);
			}

			if (AirportInfiltration::g_state.prospectorState == AirportInfiltration::PROSPECTOR_STATE_ACTIVE)
			{
				AirportInfiltration::g_state.prospectorAttackTimer -= Renderer::g_frameDelta;
				if (AirportInfiltration::g_state.prospectorAttackTimer < 0)
				{
					AirportInfiltration::g_state.prospectorAttackTimer = *g_randDatBufferPtr++ * 2 + 400;
					AudioManager::Preset::PlayOneShotSound2(0xC3, actor);
				}

				AirportInfiltration::g_state.prospectorPhaseTimer -= Renderer::g_frameDelta;
				if (AirportInfiltration::g_state.prospectorPhaseTimer < 0)
				{
					AirportInfiltration::g_state.prospectorPhaseTimer = 0;
					actor->creatureRam->defenseMode = 6;
				}
				else if (AirportInfiltration::g_state.prospectorTintToggle != 0)
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
			else
			{
				actor->useTint = 0;
			}

			if ((context->targetFlags & 1) != 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 200) != 0
				&& actor->primaryAnimIdx == 4)
			{
				actor->reservedArea[1] = 0xD0;
				actor->reservedArea[2] = 0xD0;
				Actor::SetAnimation(actor, 3, 9);
				actor->creatureRam->speedTarget = 0;
				AirportInfiltration::g_prospectorEffectTimer = 0x2C;
				if (AudioManager::IsActorSoundPlaying(actor) == 0)
					AudioManager::Preset::PlayOneShotSound2(0xC2, actor);
			}

			if (actor->primaryAnimIdx == 3 && (actor->animationFramePosition & (int32_t)0xFFFF0000) > 0x150000)
			{
				actor->movementCommandTimer = 0;
				actor->movementData = g_prospectorMovementData + 16;
				actor->creatureRam->speedTarget = 0x10;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TRACKS_TARGET | Actor::ACTOR_FLAG_TARGETS_BUZZ);
			}

			if (AirportInfiltration::g_prospectorEffectTimer != 0)
			{
				AirportInfiltration::g_prospectorEffectTimer -= Renderer::g_frameDelta;
				if (AirportInfiltration::g_prospectorEffectTimer <= 0)
				{
					int32_t yawAngle = actor->yawAngle;
					Nu3D::Particles::SpawnInstance(actor->pos.x + (Numerics::g_sinCosLUT[(yawAngle + 0x400) & 0xFFF] >> 3),
						actor->pos.y - 0x800,
						actor->pos.z + (Numerics::g_sinCosLUT[(yawAngle - 0x800) & 0xFFF] >> 3),
						Numerics::g_sinCosLUT[yawAngle] >> 2,
						0,
						Numerics::g_sinCosLUT[(yawAngle + 0x400) & 0xFFF] >> 2,
						0,
						(0x7FF - yawAngle) & 0xFFF,
						0,
						0x68);
					AudioManager::PlaySoundEffect(0xA6, &actor->pos);
					AirportInfiltration::g_prospectorEffectTimer = 0;
				}
			}

			if (AirportInfiltration::g_state.prospectorState == AirportInfiltration::PROSPECTOR_STATE_ACTIVE)
			{
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 54 / 20;
			}

			if (actor->actorPhase < 10 && AirportInfiltration::g_state.prospectorState == AirportInfiltration::PROSPECTOR_STATE_ACTIVE)
			{
				actor->movementData = g_prospectorMovementData + 45;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TARGETS_BUZZ | Actor::ACTOR_FLAG_DAMAGES_BUZZ);
				actor->movementCommandTimer = 0;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AirportInfiltration::g_state.prospectorState = AirportInfiltration::PROSPECTOR_STATE_DEFEATED;
				g_hudActorAnimationFrame = 0;
				AirportInfiltration::g_prospectorEffectTimer = 0;
				actor->creatureRam->speedTarget = 0;
			}
		}

		// FUNCTION: TOY2 0x0042C150 [MATCHED]
		void Pilot(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0 && actor->actorPhase == 0x66)
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
