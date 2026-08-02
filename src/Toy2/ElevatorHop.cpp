#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Collision.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "Toy2/TokenDialogue.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <stdlib.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace ElevatorHop
	{
		enum FramePulseIndex
		{
			FRAME_PULSE_SEVEN_TICK = 7,
			FRAME_PULSE_SIXTEEN_TICK = 9,
		};

		extern int32_t g_link18PathPointIndex;
		extern int32_t g_link19PathPointIndex;
		extern int32_t g_link20PathPointIndex;
		extern Vector3I g_fanParticleVelocityFactors[5];

		void UpdatePathLinks();
		void SpawnFanParticle(const Vector3I* position, int32_t fanIndex, int32_t velocityScale);
		void TransformMouseActors();

		enum GunsPState
		{
			GUNS_P_STATE_ACTIVE = 2,
		};

		enum GunsPActorFlags
		{
			GUNS_P_ACTOR_FLAG_VERTICAL_TARGET = 0x10,
		};

		STATIC_ASSERT(sizeof(Collectables::TokenDialogueTable<3>) == 0x34);
		STATIC_ASSERT(offsetof(Collectables::TokenDialogueTable<3>, terminator) == 0x30);

		// GLOBAL: TOY2 0x0052FC20
		int32_t g_gunsPVisualToggle;
		// GLOBAL: TOY2 0x0052FC34
		int32_t g_elevatorMotionPhase;
		// GLOBAL: TOY2 0x0052FC40
		int32_t g_gunsPAttackTimer;
		// GLOBAL: TOY2 0x0052FC48
		int32_t g_gunsPShotIndex;
		// GLOBAL: TOY2 0x0052FC4C
		GunsPState g_gunsPState;
		// GLOBAL: TOY2 0x0052FCE0
		int32_t g_gunsPPhaseTimer;
		// GLOBAL: TOY2 0x0052FCF8
		int32_t g_previousGunsPPhase;
		// GLOBAL: TOY2 0x004F3780
		int32_t g_gunsPShotDelays[6] = { 400, 40, 40, 40, 40, 200 };

		// GLOBAL: TOY2 0x004F3314
		extern const char g_elevatorInstructions[] = "if you line up the ^wire^ you will open the ^door^ and activate the ^elevators^.";
		// GLOBAL: TOY2 0x004F3368
		extern const char g_controlRoomFanInstructions[] = "the switch in the control room will activate this ^shortcut^ fan.";
		// GLOBAL: TOY2 0x004F33AC
		extern const char g_elevatorShaftFanInstructions[] = "the switch at the top of the elevator shaft will activate this ^shortcut^ fan.";

		// GLOBAL: TOY2 0x004F3614
		int16_t g_firstElevatorPlatformIds[] = { 1, 2, 3, 6, 7, 8, 9, 10, -1 };
		// GLOBAL: TOY2 0x004F3628
		int16_t g_secondElevatorPlatformIds[] = { 0, 11, 12, 13, 14, 15, 16, 17, -1 };
		// GLOBAL: TOY2 0x004F3688
		int16_t g_firstElevatorMotionScript[] = {
			8, 1, 9, 1, 8, 2, 4, 3, 0, 0x12, 9, 2, 8, 1, 9, 1, 3, 0x7F, 0x60, 8, 1, 9, 1, 8, 2, 1, 0, 0, 0, 0x12, 9, 2, 8, 1, 9, 1, 3, 0x7F, 0x20, 0, 0x27, 0
		};
		// GLOBAL: TOY2 0x004F36DC
		int16_t g_secondElevatorMotionScript[] = { 8,
			0x100,
			9,
			0x100,
			8,
			0x200,
			4,
			3,
			1,
			0xC,
			9,
			0x200,
			8,
			0x100,
			9,
			0x100,
			3,
			0x7F,
			0x20,
			8,
			0x100,
			9,
			0x100,
			8,
			0x200,
			1,
			0,
			0,
			0,
			0xC,
			9,
			0x200,
			8,
			0x100,
			9,
			0x100,
			3,
			0x7F,
			0x20,
			0,
			0x27,
			0 };
		// GLOBAL: TOY2 0x004F3798
		int16_t g_tokenLinkIds[] = { 0x6B, 0x6C, 0x6E, 0x6A, 0x6D, 0 };
		// GLOBAL: TOY2 0x004F37A4
		extern const Collectables::TokenDialogueTable<3> g_tokenDialogueValues = {
			{
				{ 0x70, 0x22, g_elevatorInstructions, 0 },
				{ 0x71, 0x23, g_controlRoomFanInstructions, 0 },
				{ 0x72, 0x24, g_elevatorShaftFanInstructions, 0 },
			},
			-1,
		};

		// GLOBAL: TOY2 0x0052FC10
		Collectables::PickupRecord* g_challengeTokenPickup;
		// GLOBAL: TOY2 0x0052FC14
		int32_t g_elevatorFlags;
		// GLOBAL: TOY2 0x0052FC18
		int32_t g_elevatorPauseDuration;
		// GLOBAL: TOY2 0x0052FC1C
		int32_t g_unusedElevatorState1;
		// GLOBAL: TOY2 0x0052FC24
		int32_t g_elevatorTriggerState;
		// GLOBAL: TOY2 0x0052FC28
		int32_t g_lowerFanPulseAngle;
		// GLOBAL: TOY2 0x0052FC2C
		int32_t g_sideDebrisTimer;
		// GLOBAL: TOY2 0x0052FC38
		int32_t g_lowerFanSpeed;
		// GLOBAL: TOY2 0x0052FC3C
		int32_t g_completionLinkScale;
		// GLOBAL: TOY2 0x0052FC44
		int32_t g_challengeTokenMoveTimer;
		// GLOBAL: TOY2 0x0052FC54
		int32_t g_lowerFanRotation;
		// GLOBAL: TOY2 0x0052FC58
		int32_t g_sideDebrisIndex;
		// GLOBAL: TOY2 0x0052FC60
		int32_t g_fallingDebrisTimer;
		// GLOBAL: TOY2 0x0052FC64
		int32_t g_flyingDebrisTimer;
		// GLOBAL: TOY2 0x0052FC68
		int32_t g_steamParticleAngle;
		// GLOBAL: TOY2 0x0052FC6C
		int32_t g_upperFanRotation;
		// GLOBAL: TOY2 0x0052FC70
		int32_t g_fallingDebrisIndex;
		// GLOBAL: TOY2 0x0052FC74
		int32_t g_flyingDebrisIndex;
		// GLOBAL: TOY2 0x0052FC78
		int32_t g_secondElevatorPlatformOriginY[9];
		// GLOBAL: TOY2 0x0052FCA0
		int32_t g_firstElevatorPlatformOriginY[9];
		// GLOBAL: TOY2 0x0052FCC8
		int32_t g_unusedElevatorState2;
		// GLOBAL: TOY2 0x0052FCCC
		int32_t g_firstElevatorMotionTimer;
		// GLOBAL: TOY2 0x0052FCD0
		int32_t g_steamSequenceTimer;
		// GLOBAL: TOY2 0x0052FCD4
		int32_t g_secondElevatorMotionTimer;
		// GLOBAL: TOY2 0x0052FCDC
		int32_t g_challengeTokenPathProgress;
		// GLOBAL: TOY2 0x0052FCE4
		int32_t g_secondElevatorMotionBlend;
		// GLOBAL: TOY2 0x0052FCE8
		int32_t g_firstElevatorMotionBlend;
		// GLOBAL: TOY2 0x0052FCEC
		int32_t g_steamSequenceIndex;
		// GLOBAL: TOY2 0x0052FCF0
		int16_t* g_secondElevatorMotionCursor;
		// GLOBAL: TOY2 0x0052FCF4
		int16_t* g_firstElevatorMotionCursor;
		// GLOBAL: TOY2 0x0052FCFC
		int32_t g_elevatorMotionCycle;
		// GLOBAL: TOY2 0x0052FD00
		int32_t g_upperFanMotionAngle;

		void Init();

		// STUB: TOY2 0x00425F60
		void Interactions() {}
	}
}

// GLOBAL: TOY2 0x0052FC30
int32_t Toy2::ElevatorHop::g_link19PathPointIndex;

// GLOBAL: TOY2 0x0052FC50
int32_t Toy2::ElevatorHop::g_link18PathPointIndex;

// GLOBAL: TOY2 0x0052FCD8
int32_t Toy2::ElevatorHop::g_link20PathPointIndex;

// GLOBAL: TOY2 0x004F37EC
Vector3I Toy2::ElevatorHop::g_fanParticleVelocityFactors[5] = {
	{ 0, 0, 0x100 },
	{ 0x100, 0, 0 },
	{ 0, -0x100, -0x100 },
	{ -0x100, 0, 0 },
	{ 0, -0x100, 0 },
};

// FUNCTION: TOY2 0x00425680 [MATCHED]
void Toy2::ElevatorHop::UpdatePathLinks()
{
	Nu3D::Link::SetPositionRawAndCommit(18,
		Levels::g_recordData[9]->data[g_link18PathPointIndex].x,
		Levels::g_recordData[9]->data[g_link18PathPointIndex].y,
		Levels::g_recordData[9]->data[g_link18PathPointIndex].z);
	Nu3D::Link::SetPositionRawAndCommit(19,
		Levels::g_recordData[9]->data[g_link19PathPointIndex + 3].x,
		Levels::g_recordData[9]->data[g_link19PathPointIndex + 3].y,
		Levels::g_recordData[9]->data[g_link19PathPointIndex + 3].z);
	Nu3D::Link::SetPositionRawAndCommit(20,
		Levels::g_recordData[9]->data[g_link20PathPointIndex + 6].x,
		Levels::g_recordData[9]->data[g_link20PathPointIndex + 6].y,
		Levels::g_recordData[9]->data[g_link20PathPointIndex + 6].z);
}

// FUNCTION: TOY2 0x00425EB0 [MATCHED]
void Toy2::ElevatorHop::SpawnFanParticle(const Vector3I* position, int32_t fanIndex, int32_t velocityScale)
{
	int32_t pulseIndex;
	if (fanIndex == 4)
	{
		pulseIndex = FRAME_PULSE_SEVEN_TICK;
		AudioManager::g_dynamicSoundFrequencies[0] = 0x1400;
		AudioManager::PlaySoundEffect(0x8C, position);
	}
	else
	{
		pulseIndex = FRAME_PULSE_SIXTEEN_TICK;
	}

	if (g_framePulseOutputs.bytes[pulseIndex] != 0)
	{
		Nu3D::Particles::ParticleInstance* particle;
		if (fanIndex != 4)
			particle = Nu3D::Particles::SpawnFromPreset(position->x, position->y, position->z, 0x4E, 2);
		else
			particle = Nu3D::Particles::SpawnFromPreset(position->x, position->y, position->z, 0x4F, 2);
		particle->velX = g_fanParticleVelocityFactors[fanIndex].x * velocityScale;
		particle->velY = g_fanParticleVelocityFactors[fanIndex].y * velocityScale;
		particle->velZ = g_fanParticleVelocityFactors[fanIndex].z * velocityScale;
		particle->groundAlignRot = 0xFFF - g_framePulsePhases.thirtyTwoTick * 0x40;
		particle->rotSpeed = -0x100;
	}
}

// FUNCTION: TOY2 0x00425A80 [PROVISIONAL]
void Toy2::ElevatorHop::TransformMouseActors()
{
	for (int32_t actorIndex = 1; actorIndex < 6; actorIndex++)
	{
		Actor::Toy2Actor* actor = &Actor::g_creatureActors[actorIndex];
		int32_t* boundaryZ = &actor->boundary.z;
		if (actor->creatureRam->latSpeedNoTarget <= 0x7F)
		{
			int32_t relativeZ = actor->pos.z - *boundaryZ;
			actor->pos.z = *boundaryZ - actor->boundary.y + actor->pos.y;
			actor->pos.y = actor->boundary.y + relativeZ;
		}
	}

	Actor::g_creatureActors[3].rollAngle = 0x800;
}

// FUNCTION: TOY2 0x00425B60 [PROVISIONAL]
void Toy2::ElevatorHop::Init()
{
	Levels::g_recordData[9]->data[2].y = -0x6A54;
	Levels::g_recordData[9]->data[5].y = -0x6A54;
	Levels::g_recordData[9]->data[8].y = -0x6A54;

	Nu3D::Link::SetPositionRawAndCommit(0x6E, Levels::g_recordData[14]->data[0].x, Levels::g_recordData[14]->data[0].y, Levels::g_recordData[14]->data[0].z);
	MoveableObject::InitTable(0);
	Collectables::Init(g_tokenLinkIds, 0x73);
	Collectables::Activate(3, 1);
	Collectables::LoadTokenTable(reinterpret_cast<const Collectables::TokenDialogueValue*>(g_tokenDialogueValues.records));

	g_challengeTokenPickup = reinterpret_cast<Collectables::PickupRecord*>(Collectables::g_tokenStates[2].verticalPosition - 1);
	g_challengeTokenPathProgress = 0;
	g_challengeTokenMoveTimer = 0;

	Platform::AddFlags(0, 0x100);
	Platform::AddFlags(1, 0x100);
	Platform::AddFlags(2, 0x100);
	Platform::AddFlags(3, 0x100);
	Platform::AddFlags(6, 0x100);
	Platform::AddFlags(7, 0x100);
	Platform::AddFlags(8, 0x100);
	Platform::AddFlags(9, 0x100);
	Platform::AddFlags(10, 0x100);
	Platform::AddFlags(11, 0x100);
	Platform::AddFlags(12, 0x100);
	Platform::AddFlags(13, 0x100);
	Platform::AddFlags(14, 0x100);
	Platform::AddFlags(15, 0x100);
	Platform::AddFlags(16, 0x100);
	Platform::AddFlags(17, 0x100);

	Vector3I origin;
	int32_t index;
	for (index = 0; g_firstElevatorPlatformIds[index] != -1; index++)
	{
		Platform::GetOrigin(g_firstElevatorPlatformIds[index], &origin);
		g_firstElevatorPlatformOriginY[index] = origin.y;
	}
	for (index = 0; g_secondElevatorPlatformIds[index] != -1; index++)
	{
		Platform::GetOrigin(g_secondElevatorPlatformIds[index], &origin);
		g_secondElevatorPlatformOriginY[index] = origin.y;
	}

	g_unusedElevatorState2 = 0;
	g_unusedElevatorState1 = 0;
	g_lowerFanPulseAngle = 0;
	g_lowerFanSpeed = 0;
	g_upperFanMotionAngle = 0;
	g_lowerFanRotation = 0;
	g_upperFanRotation = 0;
	g_elevatorMotionPhase = 0;
	g_sideDebrisTimer = 0;
	g_sideDebrisIndex = 0;
	g_flyingDebrisTimer = 0;
	g_flyingDebrisIndex = 0;
	g_fallingDebrisTimer = 0;
	g_fallingDebrisIndex = 0;
	g_gunsPState = static_cast<GunsPState>(0);
	g_elevatorMotionCycle = 0x3000;
	g_elevatorPauseDuration = 0x7FFF;
	g_gunsPAttackTimer = 400;
	g_gunsPShotIndex = 5;
	g_gunsPVisualToggle = 0;
	g_gunsPPhaseTimer = 0;
	g_previousGunsPPhase = Actor::g_creatureActors[8].actorPhase;
	g_steamParticleAngle = 0;
	g_steamSequenceIndex = 0;
	g_steamSequenceTimer = 0;
	g_elevatorFlags = 0;
	g_link18PathPointIndex = 0;
	g_link19PathPointIndex = 2;
	g_link20PathPointIndex = 0;
	g_elevatorTriggerState = 1;

	UpdatePathLinks();
	Nu3D::Link::SetScaleFromFixedOffsets(0x47, 0x1000, 0x1000, 0x1000);
	Nu3D::Link::SetScaleFromFixedOffsets(0x46, 0, 0, 0);
	Nu3D::Link::SetScaleFromFixedOffsets(0x49, 0x1000, 0x1000, 0x1000);
	Nu3D::Link::SetScaleFromFixedOffsets(0x48, 0, 0, 0);
	Nu3D::Link::SetScaleFromFixedOffsets(0x4F, 0x1000, 0x1000, 0x1000);
	Nu3D::Link::SetScaleFromFixedOffsets(0x4E, 0, 0, 0);

	g_firstElevatorMotionTimer = 0;
	g_firstElevatorMotionBlend = 0;
	g_secondElevatorMotionTimer = 0;
	g_secondElevatorMotionBlend = 0;
	g_completionLinkScale = 0x1000;
	g_firstElevatorMotionCursor = g_firstElevatorMotionScript;
	g_secondElevatorMotionCursor = g_secondElevatorMotionScript;
	Actor::g_creatureActors[8].pos.x += 0x10000;
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00425700 [PROVISIONAL]
		void GunsP(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			ElevatorHop::g_gunsPVisualToggle = (ElevatorHop::g_gunsPVisualToggle - 1) & 1;
			int32_t tintEnabled = 0;

			if (actor->actorPhase != ElevatorHop::g_previousGunsPPhase)
			{
				ElevatorHop::g_previousGunsPPhase = actor->actorPhase;
				ElevatorHop::g_gunsPPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
				AudioManager::PlaySoundEffect(0x8A, &actor->pos);
			}

			if (ElevatorHop::g_gunsPState > 1)
			{
				ElevatorHop::g_gunsPPhaseTimer -= Renderer::g_frameDelta;
				if (ElevatorHop::g_gunsPPhaseTimer < 0)
				{
					ElevatorHop::g_gunsPPhaseTimer = tintEnabled;
					actor->creatureRam->defenseMode = 7;
				}
				else if (ElevatorHop::g_gunsPVisualToggle != tintEnabled)
				{
					tintEnabled = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
			}
			actor->useTint = tintEnabled;

			if (ElevatorHop::g_gunsPState != ElevatorHop::GUNS_P_STATE_ACTIVE)
				return;

			ElevatorHop::g_gunsPAttackTimer -= Renderer::g_frameDelta;
			if (ElevatorHop::g_gunsPAttackTimer < 0)
			{
				ElevatorHop::g_gunsPShotIndex++;
				if (ElevatorHop::g_gunsPShotIndex >= 6)
					ElevatorHop::g_gunsPShotIndex = 0;
				ElevatorHop::g_gunsPAttackTimer = ElevatorHop::g_gunsPShotDelays[ElevatorHop::g_gunsPShotIndex];

				if (g_buzzActor.posAngles.pos.y > -0x1C2509)
				{
					Vector4I particlePosition;
					particlePosition.x = 0;
					particlePosition.y = -500;
					particlePosition.z = -200;
					Actor::ResolveBoneAttachmentPos(&particlePosition, actor, 0);

					if (ElevatorHop::g_gunsPShotIndex == 0)
					{
						Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
							particlePosition.x, particlePosition.y, particlePosition.z, 0, -2, 0, actor->yawAngle << 2, 0, *g_randDatBufferPtr++ - 0x80, 0x59);
						particle->pitchAngle = 0;
						AudioManager::PlaySoundEffect(0x8B, &actor->pos);
					}
					else
					{
						int32_t sine = Numerics::g_sinCosLUT[actor->yawAngle] >> 2;
						int32_t cosine = Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] >> 2;
						Nu3D::Particles::SpawnInstance(particlePosition.x, particlePosition.y, particlePosition.z, sine, -0x400, cosine, 0x40, 0, 0, 0x77);
						AudioManager::PlaySoundEffect(0x89, &actor->pos);
					}
				}
			}

			if (ElevatorHop::g_elevatorMotionPhase <= 0x400 && Actor::IsInsideBounds(&actor->pos, -0x7A00, 0x7A00, -0x7A00, 0x7A00) == 0)
			{
				actor->actorFlags &= ~ElevatorHop::GUNS_P_ACTOR_FLAG_VERTICAL_TARGET;
				actor->motionTargetPos.y = actor->boundary.y;
				if (actor->primaryAnimIdx == 2)
				{
					AudioManager::PlaySoundEffect(0x88, &actor->pos);
					Actor::SetAnimation(actor, 1, 1);
				}
			}
			else
			{
				actor->actorFlags |= ElevatorHop::GUNS_P_ACTOR_FLAG_VERTICAL_TARGET;
				actor->motionTargetPos.y = actor->boundary.y - 0x6000;
				if (actor->primaryAnimIdx == 1)
				{
					AudioManager::PlaySoundEffect(0x88, &actor->pos);
					Actor::SetAnimation(actor, 2, 2);
				}
			}

			g_hudActorAnimationFrame = actor->actorPhase * 54 / 30;
			if (g_buzzActor.posAngles.pos.y > -0x1C2509)
			{
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
				if (ElevatorHop::g_gunsPPhaseTimer == 0)
					actor->creatureRam->defenseMode = 7;
				else
					actor->creatureRam->defenseMode = 4;
			}
		}

		// FUNCTION: TOY2 0x004259B0 [MATCHED]
		void Mouse(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->actorPhase == 0x66)
			{
				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
				{
					actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x5A;
					AudioManager::PlaySoundEffect(0x8E, &actor->pos);
				}
			}

			if (actor->creatureRam->latSpeedNoTarget <= 0x7F)
			{
				actor->pitchAngle = 0xC00;
				actor->rollAngle = 0x800;
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x8E, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}

namespace Nu3D
{
	namespace Particles
	{
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
	}
}
