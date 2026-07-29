#pragma once

#include "Common.h"
#include "Numerics.h"

#include <directx6/d3d.h>

namespace Toy2
{
	namespace Actor
	{
		struct Toy2Actor;
	}

	namespace Animation
	{
		struct AnimationModel
		{
			uint8_t reserved[4];
			int32_t nodeCount;
		};

		struct ClipHeader
		{
			enum
			{
				FRAME_COUNT_MASK = 0x7FFF
			};

			int16_t headerSize;
			int16_t reserved;
			uint16_t frameCountAndFlags;
			uint16_t nodeCount;
			uint16_t sampleStride;
			uint16_t nodeOffsetCount;
			uint16_t scaleFlagByteCount;
			int16_t buzzOffsetNode;
		};

		struct KeyframeSample
		{
			int16_t translationX;
			int16_t translationY;
			int16_t translationZ;
			uint16_t packedRotationLow;
			uint16_t packedRotationHigh;
			int16_t scaleX;
			int16_t scaleY;
			int16_t scaleZ;
		};

		union RotationScratch
		{
			Vector3I16 angles;
			Matrix3x3I16 matrix;
		};

		// The animation system applies these fixed-angle offsets to each actor node.
		// The first index selects one of 64 actors. The second index selects one of 32 nodes.
		extern Vector3I g_nodeAngles[64][32];
		extern RotationScratch g_keyframeRotation;

		// Pointer to the NULL-terminated list of actors currently being animated
		// (set by AnimateActors, read by Actor::FindInActorList). NULL when idle.
		extern Actor::Toy2Actor** g_actorAnimList;
		extern int32_t g_currentActorIndex;
		extern D3DMATRIX g_nodeMatrices[64][32];
		extern D3DMATRIX g_worldNodeMatrices[64][32];
		extern AnimationModel* g_currentAnimationModel;
		extern int32_t g_identityNodeIndex;
		extern int32_t g_isLastAnimatedActor;
		extern int32_t g_applyRootNodeOffset;
		extern int32_t g_rootOffsetNodeIndex;

		// Pointers into the currently parsed animation data blob (set by ParseHeader,
		// read by SampleNodeTransform). g_nodeKeyframeOffsets is a per-node short
		// table (nodeId -> keyframe base index, -1/-2/-3 = no data), g_nodeScaleFlags
		// is a per-node bitmask (bit set = node has scale data), g_keyframeData is
		// the base of the keyframe samples.
		extern uint8_t* g_nodeKeyframeOffsets;
		extern uint8_t* g_nodeScaleFlags;
		extern uint8_t* g_keyframeData;

		void ResetNodeAngles();
		void ParseHeader(int16_t* header);
		int32_t SampleNodeTransform(int32_t nodeIndex, int16_t* clipData, int32_t framePosition, D3DMATRIX* matrix);
		void EvaluateClipToMatrices(
			int32_t actorIndex, AnimationModel* model, const D3DMATRIX* actorMatrix, int16_t* clipData, int32_t framePosition, int32_t isSecondaryTrack);

		void EvaluateClip(ClipHeader* clip, int32_t framePosition, uint16_t baseBoneIndex, int32_t track);
		void TransformByBone(Vector3I* position, void* actor, int32_t boneIndex);

		STATIC_ASSERT(sizeof(ClipHeader) == 0x10);
		STATIC_ASSERT(sizeof(KeyframeSample) == 0x10);
		STATIC_ASSERT(sizeof(RotationScratch) == 0x12);
		STATIC_ASSERT(sizeof(AnimationModel) == 8);
	}
}

namespace Nu3D
{
	namespace Bones
	{
		void GetRootWorldPos(Vector3F* position, int32_t nodeIndex);
	}
}
