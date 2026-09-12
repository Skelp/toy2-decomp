#ifndef TOY2INTERNAL_H
#define TOY2INTERNAL_H

#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/SoftwareProjectionPoint.h"
#include "Renderer/Renderer.h"

// Declarations the units of the game shell share but that no public header
// states: state one unit defines and another reads, and the creature behaviour
// and actor entry points the record tables name. Toy2.cpp held these at its
// head; the units split out of it need the same view.
void Nullsub10();
void Nullsub11();

namespace AudioManager
{
	extern char g_sfxSubPath[4];
}

namespace InputManager
{
	extern int32_t g_renderedKeyboardGlyphTextWidth;

	int32_t CalculateKeyboardGlyphTextWidth(const char* text);
	void DrawKeyboardGlyphText(int32_t x, int32_t y, const char* text);
}

namespace Renderer
{
	extern float g_parallaxHorizOffset;
	extern float g_parallaxTexHeightRatio;
	extern float g_parallaxTexWidthRatio;

	namespace Sprite
	{
		void DrawClipped(int16_t xPos, int16_t yPos, int16_t clipLeft, int16_t clipRight, int16_t sheetIndex, int16_t tileIndex);
	}

	namespace Particles
	{
		void DrawParticles();
	}

	namespace Beam
	{
		void DrawSegmentedBeam(uint32_t spriteSheetIndex,
			int32_t width,
			int32_t segmentLength,
			const Vector4I* position,
			const Vector4I* direction,
			uint32_t red,
			uint32_t green,
			uint32_t blue);
	}
}

namespace Nu3D
{
	namespace Camera
	{
		extern Vector3F g_cameraPosition;
		extern int32_t g_cameraPitch;
		extern int32_t g_cameraYaw;
		extern Vector3I g_sectorViewPosition;
		extern int32_t g_zoneViewportLeftOffset;
		extern int32_t g_zoneViewportTopOffset;
		extern int32_t g_zoneViewportRightOffset;
		extern int32_t g_zoneViewportBottomOffset;

		int32_t ProjectQuad(const SoftwareProjectionPoint* point0,
			const SoftwareProjectionPoint* point1,
			const SoftwareProjectionPoint* point2,
			const SoftwareProjectionPoint* point3,
			int32_t* projected0,
			int32_t* projected1,
			int32_t* projected2,
			int32_t* projected3,
			int32_t* unused0,
			int32_t* unused1);
		void WorldToView(const Vector3I16* source, Vector3I* destination, int32_t* viewDistance);
		int32_t IsActorSpawnVisible(const Vector3I* cameraPosition, const Toy2::Actor::Toy2Actor* actor);
	}
}

namespace Toy2
{
	extern int32_t g_zoneCount;

	namespace Level
	{
		int32_t GetSectorAtPosition(const Vector3I* position);
	}

	namespace Levels
	{
		extern int32_t g_hasZoneData;
		extern int32_t g_type63CullDistance;
		// Record type of the pickup render records.
		const int32_t RECORD_TYPE_PICKUPS = 63;
	}

	extern int32_t g_forcedFacingActive;
	extern int32_t g_forcedFacingAngle;
	extern uint32_t g_actionStateFlags;
	extern int32_t g_movementLockTimer;
	extern int32_t g_gunFireTimer;
	extern int32_t g_gunChargeTimer;
	extern int32_t g_riderPlatformOffsetX;
	extern int32_t g_riderPlatformOffsetZ;

	namespace AlsPenthouse
	{
		extern int32_t g_trainCollisionTimer;
	}

	namespace Actor
	{
		extern int32_t g_periodicHintSoundTimer;
		extern int32_t g_coinQuestHintTimer;
		extern int32_t g_coinTokenAwarded;
		extern int32_t g_rotatingHintSoundTimer;
		extern int32_t g_rotatingHintIndex;
		extern int32_t g_itemReturnHintSoundTimer;
	}
}

namespace Renderer
{}
namespace Toy2
{
	extern int32_t g_movementInputLockTimer;
	extern int32_t g_mpegPlaybackDisabled;

	namespace Actor
	{
		void UpdateAIMovement(Toy2Actor* actor);
	}

	namespace CreatureBehaviour
	{
		void Zurg3(Actor::Toy2Actor::ActorBehaviourContext* context);
		void TinMan(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Sheep(Actor::Toy2Actor::ActorBehaviourContext* context);
		void RCCar(Actor::Toy2Actor::ActorBehaviourContext* context);
		void LawnMower(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Army(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZgCar(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZKite(Actor::Toy2Actor::ActorBehaviourContext* context);
		void LTyke(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZPod(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Drill(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Mouse(Actor::Toy2Actor::ActorBehaviourContext* context);
		void BPlane(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Box(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Dino(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZBoat(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Chick(Actor::Toy2Actor::ActorBehaviourContext* context);
		void GunsP(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Clown(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Martian(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Rabid(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Buzzard(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Ducks(Actor::Toy2Actor::ActorBehaviourContext* context);
		void GunsLLevel11(Actor::Toy2Actor::ActorBehaviourContext* context);
		void GunsL(Actor::Toy2Actor::ActorBehaviourContext* context);
		void FatBloke(Actor::Toy2Actor::ActorBehaviourContext* context);
		void BBuggy(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Pilot(Actor::Toy2Actor::ActorBehaviourContext* context);
		void SmithLevel14(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Smith(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Luggage(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ProsPLevel13(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ProsP(Actor::Toy2Actor::ActorBehaviourContext* context);
	}

	namespace ElevatorHop
	{
		void TransformMouseActors();
	}
}

namespace Toy2
{
	// Frame state the shell defines and the render units read.
	extern int16_t g_isPaused;
	extern int16_t g_pickupSpriteFrame;
	extern uint8_t g_nearbyEffectRecordIndices[256];
}

namespace Toy2
{
	// State the shell defines and LevelEntry.cpp reads, and the two entry points
	// the two units call across the split.
	extern int16_t g_pauseMenuState;
	extern int16_t g_pauseMenuSelection;
	extern int16_t g_pauseMenuBlinkTimer;
	extern int32_t g_previousLevelObjectiveProgress;
	extern int32_t g_hudActorAnimationFrame;
	extern int32_t g_cameraIdleTimer;
	int32_t EnterLevel(int32_t levelIndex);

	namespace Lighting
	{
		void InitBuzzLight();
		void UpdateBuzzLight();
	}
}

#endif
