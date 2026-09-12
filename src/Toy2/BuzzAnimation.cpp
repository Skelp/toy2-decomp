#include "Toy2/Buzz.h"
#include "Toy2/BuzzInternal.h"
#include "Toy2/Animation.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/CollisionInternal.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/PoleRecord.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// The animation event track and the animation state of Buzz: retail holds
// 0x00401000 and 0x004011D0 as one object, in the order of this file.
namespace Toy2
{
	namespace Animation
	{
		// FUNCTION: TOY2 0x00401000 [PROVISIONAL]
		int32_t StepEventTrack(const uint8_t* eventTrack)
		{
			int32_t eventPosition = g_buzzActor.animationEventPosition;
			const uint8_t* event = eventTrack + (eventPosition >> 16);
			if (event[1] == 0xFF && event[2] == 1)
			{
				eventPosition &= 0xFFFF0000;
				g_buzzActor.animationEventPosition = eventPosition;
			}

			int32_t eventCode = eventTrack[eventPosition >> 16];
			while (eventCode >= 0x80)
			{
				switch (eventCode)
				{
					case 0xFF:
						eventPosition -= ((int32_t)event[1] + 1) << 16;
						g_buzzActor.animationEventPosition = eventPosition;
						break;

					case 0x81:
						if (g_buzzActor.specialAirState != 0)
							g_animationEventFlags |= Buzz::ANIMATION_EVENT_RIGHT_FOOT;
						break;

					case 0x82:
						if (g_buzzActor.specialAirState != 0)
							g_animationEventFlags |= Buzz::ANIMATION_EVENT_LEFT_FOOT;
						break;

					case 0x83:
						AudioManager::PlaySoundEffect(0x30, &g_buzzActor.posAngles.pos);
						break;

					case 0x84:
						AudioManager::PlaySoundEffect(0x10, &g_buzzActor.posAngles.pos);
						break;

					case 0x85:
						AudioManager::PlaySoundEffect(0x17, &g_buzzActor.posAngles.pos);
						break;

					case 0x86:
						AudioManager::PlaySoundEffect(0x43, &g_buzzActor.posAngles.pos);
						break;

					case 0xFE:
						g_buzzActor.animationState = 0;
						g_buzzActor.previousAnimationState = -1;
						g_idleAnimationState = 0;
						return -1;
				}

				eventPosition = g_buzzActor.animationEventPosition + 0x10000;
				g_buzzActor.animationEventPosition = eventPosition;
				event = eventTrack + (eventPosition >> 16);
				eventCode = *event;
			}

			return eventCode;
		}

	}
}

namespace Toy2
{
	namespace Buzz
	{
		// FUNCTION: TOY2 0x004011D0 [PROVISIONAL]
		void UpdateAnimationState()
		{
			for (;;)
			{
				g_idleVoiceCooldown -= Renderer::g_frameDelta;
				if (g_idleVoiceCooldown <= 0)
					g_idleVoiceCooldown = 0;

				if (g_buzzActor.specialAirState != 0)
				{
					if (g_buzzActor.animationState == ANIMATION_STATE_LANDING)
						g_buzzActor.animationState = ANIMATION_STATE_STANDING;

					if (g_buzzActor.forwardSpeed < 0x200 && abs(g_buzzActor.lateralSpeed) < 0x200
						&& (((uint8_t)InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) == 0
							|| g_idleAnimationTimer == 30))
					{
						if (g_buzzActor.animationState == ANIMATION_STATE_STANDING)
						{
							g_idleAnimationState = 0;
						}
						else
						{
							if (g_idleAnimationState == 0)
							{
								g_idleAnimationState = 1;
								if (((g_buzzActor.actorFlags & ACTOR_FLAG_BLOCK_EDGE_IDLE) == 0 || g_environmentEffectType < 2)
									&& g_buzzActor.floorYPos - g_buzzActor.posAngles.pos.y > 0x1000 && g_buzzActor.floorYPos != 0x80000000)
								{
									PosAndAngles groundProbe;
									groundProbe.pos.x =
										g_buzzActor.posAngles.pos.x + (Numerics::g_sinCosLUT[(int16_t)g_buzzActor.posAngles.angles.yaw & 0xFFF] >> 2);
									groundProbe.pos.y = g_buzzActor.posAngles.pos.y;
									groundProbe.pos.z =
										g_buzzActor.posAngles.pos.z + (Numerics::g_sinCosLUT[((int16_t)g_buzzActor.posAngles.angles.yaw + 0x400) & 0xFFF] >> 2);
									int32_t groundHeight = Nu3D::Collision::GetGroundHeight(&groundProbe, 0);
									if (groundHeight - g_buzzActor.posAngles.pos.y > 0x1000 && groundHeight != 0x80000000)
									{
										g_idleAnimationState = 10;
									}
									else
									{
										groundProbe.pos.x = g_buzzActor.posAngles.pos.x
											+ (Numerics::g_sinCosLUT[((int16_t)g_buzzActor.posAngles.angles.yaw - 0x800) & 0xFFF] >> 2);
										groundProbe.pos.z = g_buzzActor.posAngles.pos.z
											+ (Numerics::g_sinCosLUT[((int16_t)g_buzzActor.posAngles.angles.yaw - 0x400) & 0xFFF] >> 2);
										groundHeight = Nu3D::Collision::GetGroundHeight(&groundProbe, 0);
										if (groundHeight - g_buzzActor.posAngles.pos.y > 0x1000 && groundHeight != 0x80000000)
											g_idleAnimationState = 11;
									}
								}
							}

							if (g_idleAnimationState == 1 && (g_buzzActor.animationEventPosition & 0xFFFF0000) == 0xE0000)
							{
								uint8_t random = *g_randDatBufferPtr++;
								if ((random & 3) == 0)
								{
									g_idleAnimationState = (*g_randDatBufferPtr++ & 1) + 28;
									if (g_idleVoiceCooldown == 0)
									{
										g_idleVoiceCooldown = 1800;
										if (g_gunFireTimer <= 0 && g_spinHoverTimer == 0 && g_spinCooldownTimer == 0
											&& (g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
										{
											g_idleVoicePreset = (g_idleVoicePreset != 207) + 206;
											AudioManager::Preset::PlayOneShotSound(g_idleVoicePreset, &g_buzzActor);
										}
									}
								}
							}

							if (g_idleAnimationState >= 28 && (g_gunFireTimer > 0 || g_spinHoverTimer != 0 || g_spinCooldownTimer != 0))
								g_idleAnimationState = 1;

							g_buzzActor.animationState = (int16_t)g_idleAnimationState;
							if ((g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) != 0)
							{
								g_idleAnimationState = 1;
								g_buzzActor.animationState = 1;
							}
							if (g_turnRecoveryTimer != 0)
								g_buzzActor.animationState = 18;
						}
					}
					else
					{
						g_idleAnimationState = 0;
						g_buzzActor.animationState = 0;
						if (g_turnRecoveryTimer != 0)
							g_buzzActor.animationState = 18;
					}
				}
				else
				{
					g_idleAnimationState = 0;
					if (g_buzzActor.animationState == ANIMATION_STATE_FORWARD_EDGE || g_buzzActor.animationState == ANIMATION_STATE_REAR_EDGE)
						g_buzzActor.animationState = 0;
					else if (g_buzzActor.animationState == ANIMATION_STATE_AIRBORNE && g_buzzActor.velocity.vertical > 0)
						g_buzzActor.animationState = ANIMATION_STATE_LANDING;
					else if (g_buzzActor.animationState == ANIMATION_STATE_DAMAGE_REACTION)
						g_buzzActor.animationState = ANIMATION_STATE_STANDING;
					if (g_airborneTimer == 0x50)
						g_buzzActor.animationState = 12;
				}

				if (g_swingTimer != 0 && g_buzzActor.animationState != ANIMATION_STATE_SWINGING)
					g_buzzActor.animationState = SWING_START_ANIMATION_STATE;
				if (g_forcedFacingActive != 0)
					g_buzzActor.animationState = 25;
				if (g_ziplineState == ZIPLINE_RIDING)
					g_buzzActor.animationState = 17;
				if (g_grappleState == GRAPPLE_PULLING && g_grappleElapsedTime > 9)
					g_buzzActor.animationState = 30;
				if (g_gravityBootsTimer != 0)
					g_buzzActor.animationState = 31;
				if (g_spinCancelRequested != 0)
					g_buzzActor.animationState = 19;
				if (g_slipperySurfaceState != 0 && (g_buzzActor.specialAirState != 0 || g_buzzActor.animationState == 0))
					g_buzzActor.animationState = 32;
				if (g_spinHoverTimer < 0)
					g_buzzActor.animationState = g_spinHoverTimer > -120 ? 20 : 19;

				if (g_poleClimbState > 0)
				{
					g_buzzActor.animationState = 15;
					if ((g_poleClimbState & POLE_CLIMB_ASCENDING) != 0)
						g_buzzActor.animationState = 14;
					if ((g_poleClimbState & POLE_CLIMB_SLIDING) != 0)
					{
						g_buzzActor.animationState = 16;
						AudioManager::PlaySoundEffect(0x23, &g_buzzActor.posAngles.pos);
					}
				}
				if (g_ledgeClimbTimer != 0)
					g_buzzActor.animationState = 9;
				if (g_groundSlamTimer > 0)
					g_buzzActor.animationState = 23;
				if (g_groundSlamTimer < -14)
					g_buzzActor.animationState = 24;
				if (g_airborneTimer < 0)
					g_buzzActor.animationState = 13;
				if (g_rocketBootsTimer != 0)
					g_buzzActor.animationState = 10;
				if (g_buzzActor.stunTimer > 0)
				{
					if (g_buzzActor.stunTimer > 67)
						g_buzzActor.animationState = 5;
					else
						g_buzzActor.actorFlags |= ACTOR_FLAG_STUNNED;
				}
				if (g_levelTransition == 2)
					g_buzzActor.animationState = 7;
				else if (g_levelTransition == 1)
					g_buzzActor.animationState = 27;

				int32_t animationState = g_buzzActor.animationState;
				AnimationStateDefinition* definition = &g_animationStateDefinitions[animationState];
				const uint8_t* eventTrack = definition->eventTrack;
				int32_t eventSample;
				if (animationState != g_buzzActor.previousAnimationState)
				{
					g_buzzActor.animationEventPosition = 0;
					g_buzzActor.previousAnimationState = (int16_t)animationState;
					eventSample = *eventTrack;
				}
				else
				{
					int32_t frameAdvance = definition->frameAdvanceRate * Renderer::g_frameDelta;
					if (frameAdvance < 0)
					{
						int32_t movementSpeedSquared =
							g_buzzActor.lateralSpeed * g_buzzActor.lateralSpeed + g_buzzActor.forwardSpeed * g_buzzActor.forwardSpeed;
						frameAdvance = -(int32_t)sqrt((double)movementSpeedSquared) * frameAdvance;
						if (frameAdvance < 0)
							frameAdvance = 0;
					}

					while (frameAdvance > 0xFFFF)
					{
						g_buzzActor.animationEventPosition += 0x10000;
						frameAdvance -= 0x10000;
						if (Animation::StepEventTrack(eventTrack) == -1)
							break;
					}
					if (g_buzzActor.previousAnimationState == -1)
						continue;

					g_buzzActor.animationEventPosition += frameAdvance;
					eventSample = Animation::StepEventTrack(eventTrack);
					if (eventSample == -1)
						continue;
				}

				int32_t eventFramePosition = (g_buzzActor.animationEventPosition & 0xFFFF) + (eventSample << 16);
				int32_t primaryAnimationIndex = definition->primaryAnimationIndex;
				int32_t secondaryAnimationIndex = definition->secondaryAnimationIndex;
				int32_t primaryFramePosition = eventFramePosition;

				if (g_spinCooldownTimer > 0)
				{
					if (primaryAnimationIndex == secondaryAnimationIndex)
						g_spinCooldownTimer = 0;
					else
					{
						primaryAnimationIndex = 9;
						primaryFramePosition = (48 - g_spinCooldownTimer) << 15;
					}
				}
				if (g_gunFireTimer > 0)
				{
					if (primaryAnimationIndex == secondaryAnimationIndex && Camera::g_scriptedCameraState != CAMERA_STATE_VISOR)
						g_gunFireTimer = 0;
					else
					{
						primaryAnimationIndex = 26;
						primaryFramePosition = ((g_gunFireTimer & 1) + g_animationEventData[0x254 + g_gunFireTimer / 2] * 2) << 15;
					}
				}

				g_buzzActor.primaryAnimIdx = (int16_t)primaryAnimationIndex;
				g_buzzActor.animationFramePosition = primaryFramePosition;
				g_buzzActor.secondaryAnimIdx = (int16_t)secondaryAnimationIndex;
				g_buzzActor.secondaryAnimationFramePosition = eventFramePosition;
				g_buzzActor.baseAnimationFramePosition = eventFramePosition;
				if (primaryAnimationIndex == secondaryAnimationIndex)
					g_buzzActor.secondaryAnimationFramePosition = primaryFramePosition;

				if (primaryAnimationIndex != secondaryAnimationIndex)
				{
					Animation::EvaluateClip(CharacterLoader::g_characterAnimationData[0]->clips[secondaryAnimationIndex].pointer,
						g_buzzActor.secondaryAnimationFramePosition,
						0,
						0);
				}
				Animation::g_applyBuzzBoneOffset = 1;
				Animation::EvaluateClip(CharacterLoader::g_characterAnimationData[0]->clips[primaryAnimationIndex].pointer, primaryFramePosition, 0, 1);
				Animation::g_applyBuzzBoneOffset = 0;

				if (Camera::g_scriptedCameraState != 0)
				{
					g_aimTargetPosition.x = 0x5000;
					g_aimTargetPosition.y = 0x1500;
					g_aimTargetPosition.z = -0x3800;
					Nu3D::Link::TransformVectorInt3x3(0x2F, &g_aimTargetPosition);
					g_aimTargetPosition.x += g_buzzActor.posAngles.pos.x;
					g_aimTargetPosition.y += g_buzzActor.posAngles.pos.y - 0x3000;
					g_aimTargetPosition.z += g_buzzActor.posAngles.pos.z;
				}
				else
				{
					g_aimTargetPosition.x = -0x85;
					g_aimTargetPosition.y = -0xD3;
					g_aimTargetPosition.z = -6;
					Actor::ResolveBoneAttachmentPos(&g_aimTargetPosition, &g_buzzActor.boneAttachmentActor, 15);
				}

				int32_t lateralSpeed = abs(g_buzzActor.lateralSpeed);
				if (((g_buzzActor.forwardSpeed < lateralSpeed && lateralSpeed > 0x100) || g_buzzActor.forwardSpeed < -0x100) && g_buzzActor.specialAirState != 0
					&& (g_buzzActor.actorFlags & ACTOR_FLAG_LOCK_FACING) == 0 && g_airborneTimer == 0)
				{
					g_animationEventFlags |= ANIMATION_EVENT_MOVEMENT;
				}
				if (g_buzzActor.forwardSpeed == 0 && g_buzzActor.lateralSpeed == 0)
					g_animationEventFlags |= ANIMATION_EVENT_IDLE;
				return;
			}
		}
	}
}
