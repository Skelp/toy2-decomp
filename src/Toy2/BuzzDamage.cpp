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

// The damage Buzz takes: retail holds 0x004071E0 as one object.
namespace Toy2
{
	namespace Buzz
	{
		// FUNCTION: TOY2 0x004071E0 [PROVISIONAL]
		void HandleDamage(uint32_t direction, uint32_t damageFlags)
		{
			if (g_buzzActor.health < 0 || (g_gameplayStateFlags & 1) != 0)
			{
				return;
			}

			if (Camera::g_scriptedCameraState != 0)
			{
				Camera::g_scriptedCameraState = 5;
			}

			if ((damageFlags & 1) != 0)
			{
				g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[direction & 0xFFF] / 16;
				g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[(direction + 0x400) & 0xFFF] / 16;
			}

			if ((damageFlags & DAMAGE_NORMAL) != 0 && g_buzzActor.stunTimer <= 0)
			{
				g_buzzActor.actorFlags |= ACTOR_FLAG_DAMAGE_REACTION;
				if (g_buzzActor.cosmicShieldTimer == 0)
				{
					if ((int16_t)g_levelTransition != 1 && (int16_t)g_levelTransition != 5 && g_ledgeClimbTimer == 0)
					{
						goto damageTypeDispatch;
					}
				}
				else if (g_buzzActor.cosmicShieldTimer < 0)
				{
					if (g_buzzActor.stunTimer != 0)
					{
						damageFlags &= ~DAMAGE_NORMAL;
						goto damageTypeDispatch;
					}
					g_buzzActor.cosmicShieldTimer++;
				}

				g_buzzActor.stunTimer = -60;
				damageFlags &= ~DAMAGE_NORMAL;
			}

		damageTypeDispatch:
			if ((damageFlags & DAMAGE_FORCE_DEATH) != 0)
			{
				if ((int16_t)g_levelTransition != 2)
				{
					if (Camera::g_scriptedCameraState != 0)
					{
						Camera::g_scriptedCameraState = 5;
					}
					AudioManager::Preset::PlayOneShotSound2(0x15, &g_buzzActor);
					if (AudioManager::g_maxLeftVolume < 0xE)
					{
						AudioManager::g_maxLeftVolume = 0xE;
					}
					g_buzzActor.actorFlags |= ACTOR_FLAG_DEATH_TRANSITION | ACTOR_FLAG_LOCK_FACING;
					g_levelTransition = 2;
					g_levelTransitionTimer = 0x2E;
					g_buzzActor.stunTimer = -90;
					HUD::g_slideTimers[0] = 180;
				}
				return;
			}

			if ((damageFlags & DAMAGE_NORMAL) == 0)
			{
				return;
			}

			g_buzzActor.health--;
			g_jumpHeightControlActive = 0;
			g_buzzActor.airborneMode = 5;
			g_buzzActor.velocity.vertical = -0x200;
			g_buzzActor.collisionFlags = 0;
			g_buzzActor.specialAirState = 0;
			ResetBuzzState();
			g_damageRegistered = 1;
			HUD::g_slideTimers[1] = 180;

			if (g_buzzActor.health < 0)
			{
				int16_t deathTransition = 2;
				if (g_levelTransition != deathTransition)
				{
					if (Camera::g_scriptedCameraState != 0)
					{
						Camera::g_scriptedCameraState = 5;
					}
					AudioManager::Preset::PlayOneShotSound2(0x15, &g_buzzActor);
					if (AudioManager::g_maxLeftVolume < 0xE)
					{
						AudioManager::g_maxLeftVolume = 0xE;
					}
					g_buzzActor.actorFlags |= ACTOR_FLAG_LOCK_FACING;
					HUD::g_slideTimers[0] = 180;
					g_levelTransition = deathTransition;
					g_levelTransitionTimer = 120;
					g_buzzActor.stunTimer = -90;
				}
				return;
			}

			g_buzzActor.stunTimer = 90;
			if (AudioManager::IsActorSoundPlaying(&g_buzzActor) == 0)
			{
				AudioManager::Preset::PlayOneShotSound2(0x1A, &g_buzzActor);
			}
			if (AudioManager::g_maxLeftVolume < 0xE)
			{
				AudioManager::g_maxLeftVolume = 0xE;
			}
		}
	}
}
