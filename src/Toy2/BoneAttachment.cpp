#include "Toy2/Actor.h"
#include "Toy2/BuzzLightPreset.h"
#include "Toy2/Lighting.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Dialogue.h"
#include "Toy2/Gadget.h"
#include "Toy2/Levels.h"
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
#include "SaveManager.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "Toy2/Toy2Internal.h"

// The bone attachment position of an actor: retail holds 0x0043C1C0 as one
// object.
namespace Toy2
{
	namespace Actor
	{
		// FUNCTION: TOY2 0x0043C1C0 [PROVISIONAL]
		void ResolveBoneAttachmentPos(Vector4I* position, Toy2Actor* actor, int32_t boneIndex)
		{
			if (actor->primaryAnimIdx != -1)
			{
				Animation::g_singleNodeIndex = boneIndex;
				CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
				Animation::EvaluateClip(animationData->clips[actor->primaryAnimIdx].pointer, actor->animationFramePosition, animationData->baseBoneIndex, 0);
			}

			if (actor->secondaryAnimIdx != -1)
			{
				Animation::g_singleNodeIndex = boneIndex;
				CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
				Animation::EvaluateClip(
					animationData->clips[actor->secondaryAnimIdx].pointer, actor->secondaryAnimationFramePosition, animationData->baseBoneIndex, 1);
			}

			Animation::g_singleNodeIndex = -1;
			CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
			CharacterLoader::BoneTransform* boneTransform = &CharacterLoader::g_boneTransforms[animationData->baseBoneIndex + boneIndex];

			Animation::g_keyframeRotation.angles.x = actor->pitchAngle;
			Animation::g_keyframeRotation.angles.y = actor->yawAngle + 0x800;
			Animation::g_keyframeRotation.angles.z = actor->rollAngle;
			Nu3D::Math::SetRotationXYZ(&Animation::g_keyframeRotation.angles, &Animation::g_keyframeRotation.matrix);

			if (actor->scalePivotHeight != 0)
			{
				Animation::g_keyframeRotation.matrix.m00 = (int16_t)(actor->scaleX * Animation::g_keyframeRotation.matrix.m00 / 0x1000);
				Animation::g_keyframeRotation.matrix.m10 = (int16_t)(actor->scaleX * Animation::g_keyframeRotation.matrix.m10 / 0x1000);
				Animation::g_keyframeRotation.matrix.m20 = (int16_t)(actor->scaleX * Animation::g_keyframeRotation.matrix.m20 / 0x1000);
				Animation::g_keyframeRotation.matrix.m01 = (int16_t)(actor->scaleY * Animation::g_keyframeRotation.matrix.m01 / 0x1000);
				Animation::g_keyframeRotation.matrix.m11 = (int16_t)(actor->scaleY * Animation::g_keyframeRotation.matrix.m11 / 0x1000);
				Animation::g_keyframeRotation.matrix.m21 = (int16_t)(actor->scaleY * Animation::g_keyframeRotation.matrix.m21 / 0x1000);
				Animation::g_keyframeRotation.matrix.m02 = (int16_t)(actor->scaleZ * Animation::g_keyframeRotation.matrix.m02 / 0x1000);
				Animation::g_keyframeRotation.matrix.m12 = (int16_t)(actor->scaleZ * Animation::g_keyframeRotation.matrix.m12 / 0x1000);
				Animation::g_keyframeRotation.matrix.m22 = (int16_t)(actor->scaleZ * Animation::g_keyframeRotation.matrix.m22 / 0x1000);
			}

			Vector3I boneWorld;
			boneWorld.x = actor->pos.x
				+ (Animation::g_keyframeRotation.matrix.m00 * boneTransform->translation.x
					  + Animation::g_keyframeRotation.matrix.m01 * boneTransform->translation.y
					  + Animation::g_keyframeRotation.matrix.m02 * boneTransform->translation.z)
					/ 0x80;
			boneWorld.y = actor->pos.y
				+ (Animation::g_keyframeRotation.matrix.m10 * boneTransform->translation.x
					  + Animation::g_keyframeRotation.matrix.m11 * boneTransform->translation.y
					  + Animation::g_keyframeRotation.matrix.m12 * boneTransform->translation.z)
					/ 0x80;
			boneWorld.z = actor->pos.z
				+ (Animation::g_keyframeRotation.matrix.m20 * boneTransform->translation.x
					  + Animation::g_keyframeRotation.matrix.m21 * boneTransform->translation.y
					  + Animation::g_keyframeRotation.matrix.m22 * boneTransform->translation.z)
					/ 0x80;

			Nu3D::ViewMatrix::MultiplyFixed(&Animation::g_keyframeRotation.matrix, &boneTransform->rotation, &Animation::g_keyframeRotation.matrix);

			if (boneTransform->hasScale == 1)
			{
				Animation::g_keyframeRotation.matrix.m00 = (int16_t)(boneTransform->scaleX * Animation::g_keyframeRotation.matrix.m00 / 0x1000);
				Animation::g_keyframeRotation.matrix.m10 = (int16_t)(boneTransform->scaleX * Animation::g_keyframeRotation.matrix.m10 / 0x1000);
				Animation::g_keyframeRotation.matrix.m20 = (int16_t)(boneTransform->scaleX * Animation::g_keyframeRotation.matrix.m20 / 0x1000);
				Animation::g_keyframeRotation.matrix.m01 = (int16_t)(boneTransform->scaleY * Animation::g_keyframeRotation.matrix.m01 / 0x1000);
				Animation::g_keyframeRotation.matrix.m11 = (int16_t)(boneTransform->scaleY * Animation::g_keyframeRotation.matrix.m11 / 0x1000);
				Animation::g_keyframeRotation.matrix.m21 = (int16_t)(boneTransform->scaleY * Animation::g_keyframeRotation.matrix.m21 / 0x1000);
				Animation::g_keyframeRotation.matrix.m02 = (int16_t)(boneTransform->scaleZ * Animation::g_keyframeRotation.matrix.m02 / 0x1000);
				Animation::g_keyframeRotation.matrix.m12 = (int16_t)(boneTransform->scaleZ * Animation::g_keyframeRotation.matrix.m12 / 0x1000);
				Animation::g_keyframeRotation.matrix.m22 = (int16_t)(boneTransform->scaleZ * Animation::g_keyframeRotation.matrix.m22 / 0x1000);
			}

			int32_t localX = position->x;
			int32_t localY = position->y;
			int32_t localZ = position->z;
			position->x = boneWorld.x
				+ (Animation::g_keyframeRotation.matrix.m00 * localX + Animation::g_keyframeRotation.matrix.m01 * localY
					  + Animation::g_keyframeRotation.matrix.m02 * localZ)
					/ 0x80;
			position->y = boneWorld.y
				+ (Animation::g_keyframeRotation.matrix.m10 * localX + Animation::g_keyframeRotation.matrix.m11 * localY
					  + Animation::g_keyframeRotation.matrix.m12 * localZ)
					/ 0x80;
			position->z = boneWorld.z
				+ (Animation::g_keyframeRotation.matrix.m20 * localX + Animation::g_keyframeRotation.matrix.m21 * localY
					  + Animation::g_keyframeRotation.matrix.m22 * localZ)
					/ 0x80;

			if (actor->scalePivotHeight != 0)
			{
				position->y += ((0x1000 - actor->scaleY) * actor->scalePivotHeight) >> 7;
			}
		}
	}
}
