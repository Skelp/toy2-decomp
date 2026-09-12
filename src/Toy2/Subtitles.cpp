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

// The subtitle box: retail holds 0x00401C30 as one object.
namespace Toy2
{
	namespace Dialogue
	{
		// FUNCTION: TOY2 0x00401C30 [PROVISIONAL]
		void UpdateSubtitleBox()
		{
			if (g_subtitleBoxScale == 0x1000)
			{
				int32_t revealMode = 1;
				if ((InputManager::g_curButtonsPressed & INPUT_FIRE) != 0 && (InputManager::g_prevButtonsPressed & INPUT_FIRE) == 0)
				{
					g_subtitleBoxScale = -0x1000;
					AudioManager::PlaySoundEffect(0x3E, 0);
				}

				if (g_subtitlePageState != SUBTITLE_PAGE_REVEALING)
				{
					if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0)
					{
						if (g_subtitlePageState == SUBTITLE_PAGE_COMPLETE)
						{
							g_subtitleBoxScale = -0x1000;
							AudioManager::PlaySoundEffect(0x3E, 0);
						}
						else
						{
							g_subtitlePageState = SUBTITLE_PAGE_REVEALING;
							AudioManager::PlaySoundEffect(0x3D, 0);
						}
					}
				}
				else
				{
					g_subtitleCharacterDelay -= Renderer::g_frameDelta;
					if (g_subtitleCharacterDelay <= 0)
					{
						if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0)
							revealMode = 2;
						g_subtitleCharacterDelay = 2;

						int32_t column = g_subtitleColumn;
						int32_t rowStart = g_subtitleRowStart;
						char* cursor = g_subtitleTextCursor;
						int32_t style = g_subtitleCharacterStyle;
						do
						{
							if (revealMode == 1)
								revealMode = 0;
							if (column == 0)
							{
								for (int32_t i = 0; i < 18; i++)
									reinterpret_cast<uint32_t*>(&g_subtitleCells.characters[rowStart])[i] = 0x00200020;
								g_subtitleVisibleStart += 36;
								if (g_subtitleVisibleStart >= 72)
									g_subtitleVisibleStart = 0;
							}

							int32_t character = -1;
							do
							{
								character = (uint8_t)*cursor++;
								g_subtitleTextCursor = cursor;
								if (character == '^')
								{
									style ^= 0x100;
									g_subtitleCharacterStyle = style;
									character = -1;
								}
							} while (character < 0);

							g_subtitleCells.characters[rowStart + column] = (uint16_t)(style + character);
							column++;
							g_subtitleColumn = column;
							if (*cursor == '\0')
							{
								g_subtitlePageState = SUBTITLE_PAGE_COMPLETE;
								break;
							}
							if (column >= 36)
							{
								rowStart += 36;
								column = 0;
								g_subtitleColumn = 0;
								g_subtitleRowStart = rowStart;
								if (rowStart >= 72)
								{
									g_subtitleRowStart = 0;
									g_subtitlePageState = SUBTITLE_PAGE_WAITING;
									break;
								}
							}
						} while (revealMode != 0);
					}
				}

				if (g_framePulsePhases.thirtyTwoTick < 16 && g_subtitlePageState != SUBTITLE_PAGE_REVEALING)
				{
					int32_t length = 0;
					while (g_continuePrompt[length] != '\0')
						length++;
					int32_t x = (40 - length) * 4;
					char* cursor = g_continuePrompt;
					while (length > 0)
					{
						Renderer::DrawChar(x, 48, (uint8_t)*cursor++, 0xFF, 0xFF, 0xFF, 0);
						x += 8;
						length--;
					}
				}

				int32_t cellIndex = g_subtitleVisibleStart;
				if (cellIndex < 0)
					cellIndex = 0;
				int32_t x = 16;
				int32_t y = 32;
				for (int32_t i = 0; i < 72; i++)
				{
					int32_t characterStyle = (int16_t)g_subtitleCells.characters[cellIndex] >> 8;
					int32_t red;
					if (characterStyle == 1)
						red = 0;
					else
						red = 0x80;
					Renderer::DrawChar(x, y, (uint8_t)g_subtitleCells.characters[cellIndex], red, 0x80, 0, 0);
					cellIndex++;
					if (cellIndex >= 72)
						cellIndex = 0;
					x += 8;
					if (x >= 304)
					{
						x = 16;
						y += 8;
					}
				}
			}
			else if (g_subtitleBoxScale < 0)
			{
				g_subtitleBoxScale += Renderer::g_frameDelta << 8;
				if (g_subtitleBoxScale >= 0)
				{
					g_subtitleBoxScale = 0;
					g_subtitleActive = 0;
					return;
				}
			}
			else
			{
				g_subtitleBoxScale += Renderer::g_frameDelta << 8;
				if (g_subtitleBoxScale >= 0x1000)
					g_subtitleBoxScale = 0x1000;
			}

			if (g_subtitleBoxScale == 0)
				return;

			int32_t width = abs(g_subtitleBoxScale * 474);
			int32_t height = abs(g_subtitleBoxScale << 5);
			if (width < 0x4000)
				width = 0x4000;
			if (height < 0x2000)
				height = 0x2000;
			Renderer::DrawBlackBorderBox(257 - (width >> 13), 44 - (height >> 13), width, height, 0x80, 0, 0);
		}
	}
}
