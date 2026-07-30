#include "Toy2/Animation.h"

#include "CharacterLoader.h"
#include "NGNLoader/NGNLoader.h"
#include "Nu3D/Math.h"
#include "Renderer/Renderer.h"
#include "Toy2/Buzz.h"

#include <string.h>

namespace Toy2
{
	namespace Animation
	{
		struct AnimationActor
		{
			Vector3I position;
			int16_t pitchAngle;
			int16_t yawAngle;
			int16_t rollAngle;
			int16_t primaryAnimIndex;
			int16_t creatureId;
		};

		// GLOBAL: TOY2 0x00B1C3C0
		Vector3I g_nodeAngles[64][32];

		// GLOBAL: TOY2 0x00B223C0
		Actor::Toy2Actor** g_actorAnimList;

		// GLOBAL: TOY2 0x00B223DC
		int32_t g_currentActorIndex;

		// GLOBAL: TOY2 0x00B223E0
		uint8_t* g_nodeKeyframeOffsets;

		// GLOBAL: TOY2 0x00B223E8
		D3DMATRIX g_nodeMatrices[64][32];

		// GLOBAL: TOY2 0x00B423E8
		uint8_t* g_nodeScaleFlags;

		// GLOBAL: TOY2 0x00B423EC
		uint8_t* g_keyframeData;

		// GLOBAL: TOY2 0x00B423F0
		D3DMATRIX g_worldNodeMatrices[64][32];

		// GLOBAL: TOY2 0x00B62400
		Nu3D::Creature* g_currentAnimationModel;

		// GLOBAL: TOY2 0x00508D00
		int32_t g_identityNodeIndex;

		// GLOBAL: TOY2 0x00E4D98C
		int32_t g_isLastAnimatedActor;

		// GLOBAL: TOY2 0x00E4D990
		int32_t g_applyRootNodeOffset;

		// GLOBAL: TOY2 0x00E4D9A4
		int32_t g_rootOffsetNodeIndex;

		// GLOBAL: TOY2 0x0053E4B8
		Vector3I16 g_buzzBoneOffset;

		// GLOBAL: TOY2 0x0053E4BE
		int16_t g_hasBuzzBoneOffset;

		// GLOBAL: TOY2 0x0053E4C0
		int16_t g_clipHasNegativeHeader;

		// GLOBAL: TOY2 0x0053E4C4
		uint8_t* g_clipScaleFlags;

		// GLOBAL: TOY2 0x0053EEC8
		int16_t* g_clipNodeOffsets;

		// GLOBAL: TOY2 0x0053EECC
		int32_t g_singleNodeIndex = -1;

		// GLOBAL: TOY2 0x0053EED8
		int16_t g_clipHeaderSize;

		// GLOBAL: TOY2 0x00546D70
		int16_t g_applyBuzzBoneOffset;

		// GLOBAL: TOY2 0x00555354
		RotationScratch g_keyframeRotation;

		// GLOBAL: TOY2 0x00555374
		RotationScratch g_nextKeyframeRotation;

		STATIC_ASSERT(sizeof(g_nodeAngles) == 0x6000);
		STATIC_ASSERT(sizeof(g_nodeMatrices) == 0x20000);
		STATIC_ASSERT(sizeof(g_worldNodeMatrices) == 0x20000);

		// FUNCTION: TOY2 0x004CD120 [MATCHED]
		void ResetNodeAngles()
		{
			g_actorAnimList = 0;
			memset(g_nodeAngles, 0, sizeof(g_nodeAngles));
		}

		// FUNCTION: TOY2 0x004CD170 [MATCHED]
		void ParseHeader(int16_t* header)
		{
			int32_t headerSize;
			if (*header < 0)
				headerSize = -*header;
			else
				headerSize = 12;

			g_nodeKeyframeOffsets = (uint8_t*)header + headerSize;
			g_nodeScaleFlags = (uint8_t*)header + headerSize + (uint16_t)header[5] * 2;
			g_keyframeData = g_nodeScaleFlags + (uint16_t)header[6];
		}

		// FUNCTION: TOY2 0x0043BA80 [PROVISIONAL]
		void EvaluateClip(ClipHeader* clip, int32_t framePosition, uint16_t baseBoneIndex, int32_t track)
		{
			if (clip->headerSize < 0)
			{
				g_clipHeaderSize = -clip->headerSize;
				g_clipHasNegativeHeader = 1;
			}
			else
			{
				g_clipHeaderSize = sizeof(ClipHeader) - 4;
				g_clipHasNegativeHeader = 0;
			}

			g_clipNodeOffsets = (int16_t*)((uint8_t*)clip + g_clipHeaderSize);
			g_clipScaleFlags = (uint8_t*)clip + g_clipHeaderSize + clip->nodeOffsetCount * 2;
			uint8_t* keyframeData = g_clipScaleFlags + clip->scaleFlagByteCount;
			int32_t frameIndex = (int16_t)(framePosition >> 16);
			if (frameIndex > (clip->frameCountAndFlags & ClipHeader::FRAME_COUNT_MASK))
				return;

			uint16_t fraction = (uint16_t)framePosition;
			int32_t firstNode;
			int32_t nodeEnd;
			if (g_singleNodeIndex < 0)
			{
				firstNode = 0;
				nodeEnd = clip->nodeCount;
			}
			else
			{
				firstNode = g_singleNodeIndex;
				nodeEnd = g_singleNodeIndex + 1;
				baseBoneIndex += (uint16_t)g_singleNodeIndex;
			}

			for (int32_t node = firstNode; node < nodeEnd; node++)
			{
				int16_t sampleIndex = g_clipNodeOffsets[node];
				if (sampleIndex == -3)
				{
					baseBoneIndex++;
					continue;
				}
				if (sampleIndex == -2 || sampleIndex == -1)
					continue;

				CharacterLoader::BoneTransform* transform = &CharacterLoader::g_boneTransforms[baseBoneIndex];
				transform->track = (uint8_t)track;
				KeyframeSample* sample =
					reinterpret_cast<KeyframeSample*>(reinterpret_cast<int16_t*>(keyframeData) + sampleIndex + clip->sampleStride * frameIndex);
				KeyframeSample* nextSample;
				if (frameIndex < (clip->frameCountAndFlags & ClipHeader::FRAME_COUNT_MASK) - 1)
					nextSample = reinterpret_cast<KeyframeSample*>(reinterpret_cast<int16_t*>(sample) + clip->sampleStride);
				else
					nextSample = reinterpret_cast<KeyframeSample*>(reinterpret_cast<int16_t*>(keyframeData) + sampleIndex);

				if (fraction != 0)
				{
					transform->translation.x = (((nextSample->translationX >> 2) - (sample->translationX >> 2)) * fraction >> 16) + (sample->translationX >> 2);
					transform->translation.y = (((nextSample->translationY >> 2) - (sample->translationY >> 2)) * fraction >> 16) + (sample->translationY >> 2);
					transform->translation.z = (((nextSample->translationZ >> 2) - (sample->translationZ >> 2)) * fraction >> 16) + (sample->translationZ >> 2);

					uint32_t packedRotation = sample->packedRotationLow | (sample->packedRotationHigh << 16);
					g_keyframeRotation.angles.x = (int16_t)((packedRotation >> 18) & 0xffc) | (sample->translationX & 3);
					g_keyframeRotation.angles.y = (int16_t)((packedRotation >> 8) & 0xffc) | (sample->translationY & 3);
					g_keyframeRotation.angles.z = (int16_t)((packedRotation & 0x3ff) << 2) | (sample->translationZ & 3);
					Nu3D::Math::EulerToRotationMatrix(&g_keyframeRotation.angles, &g_keyframeRotation.matrix);

					packedRotation = nextSample->packedRotationLow | (nextSample->packedRotationHigh << 16);
					g_nextKeyframeRotation.angles.x = (int16_t)((packedRotation >> 18) & 0xffc) | (nextSample->translationX & 3);
					g_nextKeyframeRotation.angles.y = (int16_t)((packedRotation >> 8) & 0xffc) | (nextSample->translationY & 3);
					g_nextKeyframeRotation.angles.z = (int16_t)((packedRotation & 0x3ff) << 2) | (nextSample->translationZ & 3);
					Nu3D::Math::EulerToRotationMatrix(&g_nextKeyframeRotation.angles, &g_nextKeyframeRotation.matrix);

					transform->rotation.m00 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m00 - g_keyframeRotation.matrix.m00) * fraction >> 16) + g_keyframeRotation.matrix.m00);
					transform->rotation.m01 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m01 - g_keyframeRotation.matrix.m01) * fraction >> 16) + g_keyframeRotation.matrix.m01);
					transform->rotation.m02 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m02 - g_keyframeRotation.matrix.m02) * fraction >> 16) + g_keyframeRotation.matrix.m02);
					transform->rotation.m10 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m10 - g_keyframeRotation.matrix.m10) * fraction >> 16) + g_keyframeRotation.matrix.m10);
					transform->rotation.m11 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m11 - g_keyframeRotation.matrix.m11) * fraction >> 16) + g_keyframeRotation.matrix.m11);
					transform->rotation.m12 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m12 - g_keyframeRotation.matrix.m12) * fraction >> 16) + g_keyframeRotation.matrix.m12);
					transform->rotation.m20 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m20 - g_keyframeRotation.matrix.m20) * fraction >> 16) + g_keyframeRotation.matrix.m20);
					transform->rotation.m21 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m21 - g_keyframeRotation.matrix.m21) * fraction >> 16) + g_keyframeRotation.matrix.m21);
					transform->rotation.m22 =
						(int16_t)(((g_nextKeyframeRotation.matrix.m22 - g_keyframeRotation.matrix.m22) * fraction >> 16) + g_keyframeRotation.matrix.m22);
				}
				else
				{
					transform->translation.x = sample->translationX >> 2;
					transform->translation.y = sample->translationY >> 2;
					transform->translation.z = sample->translationZ >> 2;
					uint32_t packedRotation = sample->packedRotationLow | (sample->packedRotationHigh << 16);
					transform->rotationAngles.x = (int16_t)((packedRotation >> 18) & 0xffc) | (sample->translationX & 3);
					transform->rotationAngles.y = (int16_t)((packedRotation >> 8) & 0xffc) | (sample->translationY & 3);
					transform->rotationAngles.z = (int16_t)((packedRotation & 0x3ff) << 2) | (sample->translationZ & 3);
					Nu3D::Math::EulerToRotationMatrix(&transform->rotationAngles, &transform->rotation);
				}

				if (g_clipHasNegativeHeader && clip->offsetNode == (int16_t)baseBoneIndex)
				{
					Vector3I offset = { 0, -200, 0 };
					TransformByBone(&offset, &g_buzzActor, baseBoneIndex);
					g_buzzBoneOffset.x = (int16_t)offset.x;
					g_buzzBoneOffset.y = (int16_t)(offset.y + 200);
					g_buzzBoneOffset.z = (int16_t)offset.z;
					g_hasBuzzBoneOffset = 1;
				}

				if (g_applyBuzzBoneOffset && g_hasBuzzBoneOffset)
				{
					transform->translation.x += g_buzzBoneOffset.x;
					transform->translation.y += g_buzzBoneOffset.y;
					transform->translation.z += g_buzzBoneOffset.z;
				}

				uint8_t scaleMask = (uint8_t)(1 << (node & 7));
				if ((g_clipScaleFlags[node >> 3] & scaleMask) != 0)
				{
					if (fraction != 0)
					{
						transform->scaleX = (int16_t)(((nextSample->scaleX - sample->scaleX) * fraction >> 16) + sample->scaleX);
						transform->scaleY = (int16_t)(((nextSample->scaleY - sample->scaleY) * fraction >> 16) + sample->scaleY);
						transform->scaleZ = (int16_t)(((nextSample->scaleZ - sample->scaleZ) * fraction >> 16) + sample->scaleZ);
					}
					else
					{
						transform->scaleX = sample->scaleX;
						transform->scaleY = sample->scaleY;
						transform->scaleZ = sample->scaleZ;
					}
					transform->hasScale = 1;
				}
				else
				{
					transform->hasScale = 0;
				}

				baseBoneIndex++;
			}

			if (g_applyBuzzBoneOffset && g_hasBuzzBoneOffset)
				g_hasBuzzBoneOffset = 0;
			g_singleNodeIndex = -1;
		}

		// FUNCTION: TOY2 0x0043C0E0 [PROVISIONAL]
		void TransformByBone(Vector3I* position, void* actor, int32_t boneIndex)
		{
			AnimationActor* animationActor = (AnimationActor*)actor;
			CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[animationActor->creatureId];
			CharacterLoader::BoneTransform* transform = &CharacterLoader::g_boneTransforms[animationData->baseBoneIndex + boneIndex];
			int32_t x = position->x;
			int32_t y = position->y;
			int32_t z = position->z;
			Vector3I transformed;
			transformed.x = (transform->rotation.m00 * x + transform->rotation.m01 * y + transform->rotation.m02 * z) / 4096;
			transformed.y = (transform->rotation.m10 * x + transform->rotation.m11 * y + transform->rotation.m12 * z) / 4096;
			transformed.z = (transform->rotation.m20 * x + transform->rotation.m21 * y + transform->rotation.m22 * z) / 4096;
			position->x = transform->translation.x + transformed.x;
			position->y = transform->translation.y + transformed.y;
			position->z = transform->translation.z + transformed.z;
		}

		// FUNCTION: TOY2 0x004CD1B0 [PROVISIONAL]
		int32_t SampleNodeTransform(int32_t nodeIndex, int16_t* clipData, int32_t framePosition, D3DMATRIX* matrix)
		{
			ClipHeader* clip = (ClipHeader*)clipData;
			int32_t fraction = (uint16_t)framePosition;
			float blend = (float)fraction * (1.0f / 65535.0f);
			uint32_t lastFrame = clip->frameCountAndFlags - 2;
			uint32_t frame = (uint32_t)framePosition >> 16;
			if (frame > lastFrame)
				frame = lastFrame;

			uint32_t nextFrame = frame + 1;
			if ((int32_t)frame >= (int32_t)lastFrame)
				nextFrame = frame;

			int16_t* frameData = (int16_t*)g_keyframeData + clip->sampleStride * frame;
			int32_t sampleOffset = ((int16_t*)g_nodeKeyframeOffsets)[nodeIndex];
			if (sampleOffset == -3 || sampleOffset == -2 || sampleOffset == -1)
				return 0;

			KeyframeSample* sample = (KeyframeSample*)(frameData + sampleOffset);
			KeyframeSample* nextSample = (KeyframeSample*)((int16_t*)g_keyframeData + clip->sampleStride * nextFrame + sampleOffset);
			Vector3F translation;
			D3DMATRIX rotation;

			if (fraction != 0)
			{
				translation.x = (float)((((nextSample->translationX >> 2) - (sample->translationX >> 2)) * fraction >> 16) + (sample->translationX >> 2));
				translation.y = (float)((((nextSample->translationY >> 2) - (sample->translationY >> 2)) * fraction >> 16) + (sample->translationY >> 2));
				translation.z = (float)((((nextSample->translationZ >> 2) - (sample->translationZ >> 2)) * fraction >> 16) + (sample->translationZ >> 2));

				uint32_t packedRotation = sample->packedRotationLow | (sample->packedRotationHigh << 16);
				int32_t pitch = ((sample->translationX & 3) << 4) | ((int32_t)packedRotation >> 14 & 0xffc0);
				int32_t yaw = ((sample->translationY & 3) << 4) | ((int32_t)packedRotation >> 4 & 0xffc0);
				int32_t roll = (((packedRotation & 0x3ff) << 2) | (sample->translationZ & 3)) << 4;

				packedRotation = nextSample->packedRotationLow | (nextSample->packedRotationHigh << 16);
				int32_t nextPitch = ((nextSample->translationX & 3) << 4) | ((int32_t)packedRotation >> 14 & 0xffc0);
				int32_t nextYaw = ((nextSample->translationY & 3) << 4) | ((int32_t)packedRotation >> 4 & 0xffc0);
				int32_t nextRoll = (((packedRotation & 0x3ff) << 2) | (nextSample->translationZ & 3)) << 4;

				Nu3D::Math::BuildIdentityMatrix(&rotation);
				Nu3D::Math::MatrixRotatePitch(&rotation, pitch);
				Nu3D::Math::MatrixRotateYaw(&rotation, yaw);
				Nu3D::Math::MatrixRotateRoll(&rotation, roll);
				D3DMATRIX nextRotation;
				Nu3D::Math::BuildIdentityMatrix(&nextRotation);
				Nu3D::Math::MatrixRotatePitch(&nextRotation, nextPitch);
				Nu3D::Math::MatrixRotateYaw(&nextRotation, nextYaw);
				Nu3D::Math::MatrixRotateRoll(&nextRotation, nextRoll);

				rotation._11 = (nextRotation._11 - rotation._11) * blend + rotation._11;
				rotation._12 = (nextRotation._12 - rotation._12) * blend + rotation._12;
				rotation._13 = (nextRotation._13 - rotation._13) * blend + rotation._13;
				rotation._21 = (nextRotation._21 - rotation._21) * blend + rotation._21;
				rotation._22 = (nextRotation._22 - rotation._22) * blend + rotation._22;
				rotation._23 = (nextRotation._23 - rotation._23) * blend + rotation._23;
				rotation._31 = (nextRotation._31 - rotation._31) * blend + rotation._31;
				rotation._32 = (nextRotation._32 - rotation._32) * blend + rotation._32;
				rotation._33 = (nextRotation._33 - rotation._33) * blend + rotation._33;
			}
			else
			{
				translation.x = (float)(sample->translationX >> 2);
				translation.y = (float)(sample->translationY >> 2);
				translation.z = (float)(sample->translationZ >> 2);

				uint32_t packedRotation = sample->packedRotationLow | (sample->packedRotationHigh << 16);
				int32_t pitch = ((nextSample->translationX & 3) << 4) | ((int32_t)packedRotation >> 14 & 0xffc0);
				int32_t yaw = ((nextSample->translationY & 3) << 4) | ((int32_t)packedRotation >> 4 & 0xffc0);
				int32_t roll = (((packedRotation & 0x3ff) << 2) | (nextSample->translationZ & 3)) << 4;
				Nu3D::Math::BuildIdentityMatrix(&rotation);
				Nu3D::Math::MatrixRotatePitch(&rotation, pitch);
				Nu3D::Math::MatrixRotateYaw(&rotation, yaw);
				Nu3D::Math::MatrixRotateRoll(&rotation, roll);
			}

			Vector3F scale;
			if ((g_nodeScaleFlags[nodeIndex / 8] & (1 << (nodeIndex % 8))) != 0)
			{
				if (fraction != 0)
				{
					scale.x = (float)(((nextSample->scaleX - sample->scaleX) * fraction >> 16) + sample->scaleX) * (1.0f / 4096.0f);
					scale.y = (float)(((nextSample->scaleY - sample->scaleY) * fraction >> 16) + sample->scaleY) * (1.0f / 4096.0f);
					scale.z = (float)(((nextSample->scaleZ - sample->scaleZ) * fraction >> 16) + sample->scaleZ) * (1.0f / 4096.0f);
				}
				else
				{
					scale.x = (float)sample->scaleX * (1.0f / 4096.0f);
					scale.y = (float)sample->scaleY * (1.0f / 4096.0f);
					scale.z = (float)sample->scaleZ * (1.0f / 4096.0f);
				}
			}
			else
			{
				scale.x = 1.0f;
				scale.y = 1.0f;
				scale.z = 1.0f;
			}

			if (clip->offsetNode == nodeIndex)
				g_rootOffsetNodeIndex = clip->offsetNode;
			if (g_applyRootNodeOffset != 0 && g_rootOffsetNodeIndex != 0)
			{
				Vector3F rootPosition = { 0.0f, -200.0f, 0.0f };
				Nu3D::Bones::GetRootWorldPos(&rootPosition, g_rootOffsetNodeIndex);
				translation.x += rootPosition.x;
				translation.y += rootPosition.y + 200.0f;
				translation.z += rootPosition.z;
			}

			Nu3D::Math::MatrixApplyScale(matrix, &scale);
			Nu3D::Math::MultiplyMatrix3x4(matrix, matrix, &rotation);
			Nu3D::Math::AddWorldSpaceTransform(matrix, &translation);
			return 1;
		}

		// FUNCTION: TOY2 0x004CD7B0 [PROVISIONAL]
		void EvaluateClipToMatrices(
			int32_t actorIndex, Nu3D::Creature* creature, const D3DMATRIX* actorMatrix, int16_t* clipData, int32_t framePosition, int32_t isSecondaryTrack)
		{
			if (clipData == 0)
			{
				return;
			}

			if (isSecondaryTrack != 0)
			{
				g_rootOffsetNodeIndex = 0;
				g_applyRootNodeOffset = 0;
			}
			else
			{
				g_applyRootNodeOffset = g_isLastAnimatedActor;
			}

			ParseHeader(clipData);
			for (int32_t nodeIndex = 0; nodeIndex < creature->nodeCount; nodeIndex++)
			{
				D3DMATRIX nodeMatrix;
				Nu3D::Math::BuildIdentityMatrix(&nodeMatrix);
				if (nodeIndex == g_identityNodeIndex || SampleNodeTransform(nodeIndex, clipData, framePosition, &nodeMatrix) != 0 || isSecondaryTrack != 0)
				{
					g_nodeMatrices[actorIndex][nodeIndex] = nodeMatrix;
					Nu3D::Math::MultiplyMatrix3x4(&g_worldNodeMatrices[actorIndex][nodeIndex], &nodeMatrix, actorMatrix);
				}
			}
		}

		// FUNCTION: TOY2 0x004CD880 [PROVISIONAL]
		void AnimateActors(Actor::Toy2Actor** actors)
		{
			g_actorAnimList = actors;
			int32_t actorIndex = 0;
			if (NGNLoader::g_ngnImage->creatureCount != 0 && *actors != 0)
			{
				Actor::Toy2Actor** actorCursor = actors;
				do
				{
					g_isLastAnimatedActor = actorCursor[1] == 0;
					Actor::Toy2Actor* actor = *actorCursor;
					Nu3D::Creature* creature = NGNLoader::g_ngnImage->creatureData[actor->creatureId];
					if (creature != 0)
					{
						g_currentAnimationModel = creature;
						g_currentActorIndex = actorIndex;

						D3DMATRIX secondaryActorMatrix;
						D3DMATRIX primaryActorMatrix;
						Nu3D::Math::BuildIdentityMatrix(&secondaryActorMatrix);
						Nu3D::Math::BuildIdentityMatrix(&primaryActorMatrix);

						float secondaryHeightOffset = 0.0f;
						Vector3F transformVector;
						transformVector.x = (float)actor->scaleX * (1.0f / 4096.0f);
						transformVector.y = (float)actor->scaleY * (1.0f / 4096.0f);
						transformVector.z = (float)actor->scaleZ * (1.0f / 4096.0f);
						if (actor->scalePivotHeight != 0)
						{
							if (actor->scalePivotHeight == (int16_t)0x8000)
								Nu3D::Math::MatrixApplyScale(&primaryActorMatrix, &transformVector);

							uint16_t scalePivotHeight = (uint16_t)actor->scalePivotHeight;
							if (scalePivotHeight == 0x8000 || (scalePivotHeight & 1) != 0)
							{
								if (scalePivotHeight != 0x8000)
									secondaryHeightOffset = (1.0f - transformVector.y) * actor->scalePivotHeight;
								Nu3D::Math::MatrixApplyScale(&secondaryActorMatrix, &transformVector);
							}
						}

						Nu3D::Math::MatrixRotatePitch(&secondaryActorMatrix, actor->pitchAngle << 4);
						Nu3D::Math::MatrixRotateYaw(&secondaryActorMatrix, (actor->yawAngle + 0x800) << 4);
						Nu3D::Math::MatrixRotateRoll(&secondaryActorMatrix, actor->rollAngle << 4);
						Nu3D::Math::MatrixRotatePitch(&primaryActorMatrix, actor->pitchAngle << 4);
						Nu3D::Math::MatrixRotateYaw(&primaryActorMatrix, (actor->yawAngle + 0x800) << 4);
						Nu3D::Math::MatrixRotateRoll(&primaryActorMatrix, actor->rollAngle << 4);

						transformVector.x = (float)actor->pos.x * Renderer::LensFlare::k_positionScale;
						transformVector.y = (float)actor->pos.y * Renderer::LensFlare::k_positionScale;
						transformVector.z = (float)actor->pos.z * Renderer::LensFlare::k_positionScale;
						Nu3D::Math::AddWorldSpaceTransform(&primaryActorMatrix, &transformVector);
						transformVector.y = secondaryHeightOffset + transformVector.y;
						Nu3D::Math::AddWorldSpaceTransform(&secondaryActorMatrix, &transformVector);

						if (actor->secondaryAnimIdx != -1)
						{
							EvaluateClipToMatrices(actorIndex,
								creature,
								&secondaryActorMatrix,
								creature->animData[actor->secondaryAnimIdx],
								actor->secondaryAnimationFramePosition,
								1);
						}
						if (actor->primaryAnimIdx != -1)
						{
							EvaluateClipToMatrices(
								actorIndex, creature, &primaryActorMatrix, creature->animData[actor->primaryAnimIdx], actor->animationFramePosition, 0);
						}

						for (int32_t nodeIndex = 0; nodeIndex < creature->nodeCount; nodeIndex++)
						{
							Vector3I* nodeAngles = &g_nodeAngles[actorIndex][nodeIndex];
							D3DMATRIX* nodeMatrix = &g_worldNodeMatrices[actorIndex][nodeIndex];
							if (nodeAngles->z != 0)
							{
								Nu3D::Math::MatrixRotateRoll(nodeMatrix, nodeAngles->z << 4);
								nodeAngles->z = 0;
							}
							if (nodeAngles->y != 0)
							{
								Nu3D::Math::MatrixRotateYaw(nodeMatrix, nodeAngles->y << 4);
								nodeAngles->y = 0;
							}
							if (nodeAngles->x != 0)
							{
								Nu3D::Math::MatrixRotatePitch(nodeMatrix, nodeAngles->x << 4);
								nodeAngles->x = 0;
							}
						}
					}

					g_currentAnimationModel = 0;
					actorCursor++;
					actorIndex++;
				} while (*actorCursor != 0);
			}
		}
	}
}

namespace Nu3D
{
	namespace Bones
	{
		// FUNCTION: TOY2 0x004CD140 [MATCHED]
		void GetRootWorldPos(Vector3F* position, int32_t nodeIndex)
		{
			if (Toy2::Animation::g_currentAnimationModel != 0)
			{
				Math::TransformPointByMatrix(position, position, &Toy2::Animation::g_nodeMatrices[Toy2::Animation::g_currentActorIndex][nodeIndex]);
			}
		}
	}
}
