#include "Toy2/Toy2.h"
#include "Toy2/Gadget.h"
#include "Toy2/Ini.h"
#include "Toy2/Screens.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"
#include "Logger.h"
#include "FileUtils.h"
#include "InputManager.h"
#include "DrawingDevice.h"
#include "ModeSelect.h"
#include "Nullsub.h"
#include "SoftwareRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Renderer/Glue.h"
#include "Renderer/Shadows.h"
#include "Toy2/KiteTail.h"
#include "SaveManager.h"
#include "Random.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/PoleRecord.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Weather.h"
#include "Nu3D/Font.h"
#include "Nu3D/FMV.h"
#include "Nu3D/Link.h"
#include "Nu3D/Viewport.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Light.h"
#include "Nu3D/Scene.h"
#include "Nu3D/SoftwareProjectionPoint.h"
#include "Renderer/Renderer.h"
#include "AudioManager/AudioManager.h"
#include "NGNLoader/NGNLoader.h"
#include <WINDOWS.H>
#include <STDIO.H>
#include <STRING.H>
#include <DINPUT.H>
#include <LIMITS.H>
#include <MATH.H>
#include <STDLIB.H>
#include <Numerics.h>
#include "Toy2/Toy2Internal.h"
#include "Toy2/Toy2Internal.h"

// The per-level variables and the interactions they drive: retail holds
// 0x004A1A50 and 0x004A1B00 as one object, in the order of this file.
namespace Toy2
{
	// FUNCTION: TOY2 0x004A1A50 [MATCHED]
	void InitialiseLevelVariables(int32_t levelIndex)
	{
		switch (levelIndex)
		{
			case 1:
				AndysHouse::Init();
				break;
			case 2:
				AndysNeighborhood::Init();
				break;
			case 3:
				BombsAway::Init();
				break;
			case 4:
				ConstructionYard::Init();
				break;
			case 5:
				AlleysAndGullies::Init();
				break;
			case 6:
				SlimeTime::Init();
				break;
			case 7:
				AlsToyBarn::Init();
				break;
			case 8:
				AlsSpaceLand::Init();
				break;
			case 9:
				BarnEncounter::Init();
				break;
			case 10:
				ElevatorHop::Init();
				break;
			case 11:
				AlsPenthouse::Init();
				break;
			case 12:
				EvilEmperorZurg::Init();
				break;
			case 13:
				AirportInfiltration::Init();
				break;
			case 14:
				TarmacTrouble::Init();
				break;
			case 15:
				FinalShowdown::Init();
				break;
			case 16:
				InitialiseLevel16();
				break;
			case 17:
				InitialiseLevel17();
				break;
		}
	}

	// FUNCTION: TOY2 0x004A1B00 [MATCHED]
	void HandleLevelInteractions(int32_t levelIndex)
	{
		switch (levelIndex)
		{
			case 1:
				AndysHouse::Interactions();
				break;
			case 2:
				AndysNeighborhood::Interactions();
				break;
			case 3:
				BombsAway::Interactions();
				break;
			case 4:
				ConstructionYard::Interactions();
				break;
			case 5:
				AlleysAndGullies::Interactions();
				break;
			case 6:
				SlimeTime::Interactions();
				break;
			case 7:
				AlsToyBarn::Interactions();
				break;
			case 8:
				AlsSpaceLand::Interactions();
				break;
			case 9:
				BarnEncounter::Interactions();
				break;
			case 10:
				ElevatorHop::Interactions();
				break;
			case 11:
				AlsPenthouse::Interactions();
				break;
			case 12:
				EvilEmperorZurg::Interactions();
				break;
			case 13:
				AirportInfiltration::Interactions();
				break;
			case 14:
				TarmacTrouble::Interactions();
				break;
			case 15:
				FinalShowdown::Interactions();
				break;
			case 16:
				HandleLevel16Interactions();
				break;
			case 17:
				HandleLevel17Interactions();
				break;
		}
	}
}
