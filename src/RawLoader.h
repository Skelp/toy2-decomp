#pragma once

#include "Common.h"
#include "Numerics.h"

namespace RawLoader
{
	enum CreatureDefenseMode
	{
		CREATURE_DEFENSE_SPECIAL_ATTACK_DAMAGE = 0x2,
	};

	struct CreatureListRam
	{
		struct EntityControl
		{
			uint8_t actorPhase;
			uint8_t respawnDelay;
			uint16_t actorFlags;
		};

		Vector3I pos;
		uint8_t creatureId;
		uint8_t movCtrl;
		uint8_t rotSpeed;
		uint8_t initialFacingAngle;
		EntityControl entCtrl;
		int16_t boundHalfX;
		int16_t boundHalfZ;
		int16_t boundAngle;
		int8_t defenseMode;
		uint8_t defenseReserved;
		uint8_t latSpeedNoTarget;
		uint8_t latSpeedTarget;
		uint8_t speedNoTarget;
		uint8_t speedTarget;
	};

	extern CreatureListRam g_creatureListRam[64];

	void LoadRawAndNGN(char* fileName);
	void LoadPacketData(char* fileName);

	STATIC_ASSERT(sizeof(CreatureListRam) == 0x20);
	STATIC_ASSERT(sizeof(CreatureListRam::EntityControl) == 0x4);
}
