#include "Toy2/Levels.h"
#include "SoftwareRenderer.h"
#include "FileUtils.h"
#include "Random.h"
#include "Logger.h"
#include "CharacterLoader.h"
#include "RawLoader.h"
#include "Collision.h"
#include "NGNLoader/NGNLoader.h"
#include "Renderer/Glue.h"
#include "Toy2/Toy2.h"
#include "Toy2/Collectables.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Camera.h"
#include "AudioManager/AudioManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Light.h"
#include "SaveManager.h"
#include "Toy2/Direct6.h"
#include "Toy2/Weather.h"
#include "Toy2/LevelsInternal.h"

// The level bin path and the play set-up it feeds: retail holds 0x00452EF0
// and 0x00452FC0 as one object, in the order of this file.
namespace Toy2
{
	namespace Levels
	{
		// FUNCTION: TOY2 0x00452EF0 [MATCHED]
		void BuildLevelBinPath(int32_t level, char* output, int32_t useAlternateRange)
		{
			strcpy(output, "..\\level");

			if (useAlternateRange > 0)
				level += 10;

			char digits[4];
			if (level < 10)
			{
				digits[0] = '0';
				digits[1] = level + '0';
			}
			else
			{
				digits[0] = level / 10 + '0';
				digits[1] = level % 10 + '0';
			}

			digits[2] = '\0';
			strcat(output, digits);
			strcat(output, ".bin");
		}

		// FUNCTION: TOY2 0x00452FC0 [PROVISIONAL]
		void InitLevelPlay(int32_t levelId)
		{
			SoftwareRenderer::SetLevelFileIndex(g_levelFileIndex);
			FlushRenderer();
			InitLevelDefaults();

			FileUtils::LoadFile("rand.dat", g_randDatBuffer);

			AudioManager::g_curTrackIndex = -1;
			g_isElevatorHopLevel = levelId != 10;

			Logger::Log("InitLevelPlay : START.\n");

			AudioManager::LoadSfxPackForLevel(levelId);

			memset(&g_recordData, 0, sizeof(g_recordData));
			memset(CharacterLoader::g_boneTransforms, 0, sizeof(CharacterLoader::g_boneTransforms));
			memset(Collision::g_collisionMeshInstances, 0, sizeof(Collision::g_collisionMeshInstances));
			memset(Collision::g_mathScratch, 0, sizeof(Collision::g_mathScratch));

			SoftwareRenderer::g_backdropScrollOverride.x = kBackdropScrollUnset;
			SoftwareRenderer::g_backdropScrollOverride.y = kBackdropScrollUnset;

			g_hasBackdrop = 0;

			CharacterLoader::g_boneRemapCount = 0;
			CharacterLoader::g_processedBoneRemapCount = 0;
			CharacterLoader::g_animationSlotCount = 0;

			SoftwareRenderer::g_backdropTextureColumn = -1;

			Renderer::Shadows::g_shadowCount = 0;
			Renderer::Shadows::g_unusedShadowVar = 0;

			// Stop all particle instances
			for (int32_t particleIdx = 0; particleIdx < kMaxParticleInstances; particleIdx++)
				Nu3D::Particles::g_particleInstances[particleIdx].lifetime = 0;

			Nu3D::Camera::InitViewMatrixGlobals();

			memset(CharacterLoader::g_alternateAllParse, 0, sizeof(CharacterLoader::g_alternateAllParse));
			memset(CharacterLoader::g_charFileDataCache, 0, sizeof(CharacterLoader::g_charFileDataCache));

			g_cachedAllBuffer = 0;

			int32_t requestedLevelId = levelId;

			// Map a bonus level to its base level
			if (levelId > 10)
			{
				g_levelLoadConfig |= kLoadBonusVariant;

				if ((g_levelLoadConfig & kLoadTextureSetMask) == 0)
					g_levelLoadConfig |= kLoadTextureSet1;

				levelId -= 10;
			}

			int32_t loadConfigCpy = g_levelLoadConfig;
			g_initialLevelLoadConfig = g_levelLoadConfig;

			uint8_t* dataBuffer = g_levelDataHeapBase;

			g_levelDataHeapBasePtr = g_levelDataHeapBase;
			g_levelLoadArena = g_levelDataHeapBase;

			char fileNameBuffer[kLevelFileNameSize];
			fileNameBuffer[0] = '\0';

			// Load the level packet file
			if ((loadConfigCpy & 1) == 0)
			{
				// Select the packet file of the requested raw variant
				switch ((loadConfigCpy >> 6) & 3)
				{
					case 1:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level1.raw");
						break;

					case 2:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level2.raw");
						break;

					case 3:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level3.raw");
						break;

					default:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level.raw");
						break;
				}

				RawLoader::LoadPacketData(fileNameBuffer);
				loadConfigCpy = g_levelLoadConfig;
			}

			dataBuffer = g_levelLoadArena;

			char levelDigits[4];
			int32_t binLevelId;

			// Load the level binary
			if ((loadConfigCpy & 2) == 0)
			{
				// Build the path of the level binary
				switch ((loadConfigCpy >> 8) & 3)
				{
					case 1:
						strcpy(fileNameBuffer, "..\\level");
						binLevelId = levelId + 10;

						if (binLevelId < 10)
						{
							levelDigits[0] = '0';
							levelDigits[1] = binLevelId + '0';
						}
						else
						{
							levelDigits[0] = binLevelId / 10 + '0';
							levelDigits[1] = binLevelId % 10 + '0';
						}

						levelDigits[2] = '\0';
						break;

					case 2:
						strcpy(fileNameBuffer, "..\\level");

						binLevelId = levelId + 10;

						if (binLevelId < 10)
						{
							levelDigits[0] = '0';
							levelDigits[1] = binLevelId + '0';
							levelDigits[2] = '\0';
						}
						else
						{
							levelDigits[0] = binLevelId / 10 + '0';
							levelDigits[1] = binLevelId % 10 + '0';
							levelDigits[2] = '\0';
						}
						break;

					case 3:
						strcpy(fileNameBuffer, "..\\level");
						binLevelId = levelId + 10;

						if (binLevelId < 10)
						{
							levelDigits[0] = '0';
							levelDigits[1] = binLevelId + '0';
							levelDigits[2] = '\0';
						}
						else
						{
							levelDigits[2] = '\0';
							levelDigits[0] = binLevelId / 10 + '0';
							levelDigits[1] = binLevelId % 10 + '0';
						}
						break;

					default:
						strcpy(fileNameBuffer, "..\\level");

						if (levelId < 10)
						{
							levelDigits[0] = '0';
							levelDigits[2] = '\0';
							levelDigits[1] = levelId + '0';
						}
						else
						{
							levelDigits[2] = '\0';
							levelDigits[0] = levelId / 10 + '0';
							levelDigits[1] = levelId % 10 + '0';
						}
						break;
				}

				strcat(fileNameBuffer, levelDigits);
				strcat(fileNameBuffer, ".bin");
			}

			Renderer::InitSpriteSheets();

			void* bufferPtr;
			int32_t loadedCharacterBoneCount = 0;

			memset(fileNameBuffer, 0, sizeof(fileNameBuffer));

			// Load the level image
			if ((g_levelLoadConfig & 4) == 0)
			{
				bufferPtr = dataBuffer;
				g_levelDataBase = dataBuffer;

				switch ((g_levelLoadConfig >> 8) & 3)
				{
					case 1:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level1.dat");
						break;

					case 2:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level2.dat");
						break;

					case 3:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level3.dat");
						break;

					default:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level.dat");
						break;
				}

				int32_t fileSize = FileUtils::LoadFile(fileNameBuffer, bufferPtr);
				dataBuffer = dataBuffer + fileSize;

				int32_t offset = LoadDAT(levelId, fileSize);
				dataBuffer = dataBuffer + offset;
			}

			fileNameBuffer[0] = 0;
			FileUtils::AppendRegPathToBuffer();

			// Load the level geometry image unless the level data is cached
			if ((g_levelLoadConfig & 4) == 0 || (g_levelLoadConfig & 1) == 0)
			{
				g_levelDataBase = dataBuffer;

				switch ((g_levelLoadConfig >> 8) & 3)
				{
					case 1:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level1");
						break;

					case 2:
						if (levelId < 10)
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelId + '0';
						}
						else
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelId / 10 + '0';
							fileNameBuffer[6] = levelId % 10 + '0';
						}

						fileNameBuffer[7] = '\\';
						fileNameBuffer[8] = '\0';
						strcat(fileNameBuffer, "level2");
						break;

					case 3:
						BuildLevelPath(levelId, fileNameBuffer, "level3");
						break;

					default:
						BuildLevelPath(levelId, fileNameBuffer, "level");
						break;
				}

				if ((g_levelLoadConfig & 4) != 0 || levelId == 0 || requestedLevelId == kTextureSuffixLevelId)
				{
					// Add the texture set suffix
					switch ((g_levelLoadConfig >> 6) & 3)
					{
						case 1:
							strcat(fileNameBuffer, "t1");
							break;
						case 2:
							strcat(fileNameBuffer, "t2");
							break;
						case 3:
							strcat(fileNameBuffer, "t3");
							break;
					}
				}

				strcat(fileNameBuffer, ".ngn");
				strcat(FileUtils::g_fileNameBuffer, fileNameBuffer);
				NGNLoader::SetNewImage(FileUtils::g_fileNameBuffer);
				NGNLoader::DetectBackdropTextures();
			}

			// Build the collision world and the pickup table
			if ((g_levelLoadConfig & 8) == 0)
			{
				if (! g_cachedAllBuffer)
					Collision::BuildCollisionWorld(levelId, &dataBuffer, (g_levelLoadConfig >> 8) & 3);

				if ((g_levelLoadConfig & 8) == 0)
					Collectables::BuildPickupTable();
			}

			// Load the creatures of the level
			if ((g_levelLoadConfig & kLoadSkipCreatures) == 0)
			{
				uint8_t creatureIdList[kMaxCreatureIds];
				Actor::GetCreatureList(creatureIdList);
				CharacterLoader::Start(&loadedCharacterBoneCount, &dataBuffer, creatureIdList);
			}

			g_levelLoadArena = dataBuffer;
			g_levelLoadConfig = 0;

			Logger::Log("InitLevelPlay : END.\n");
		}
	}
}
