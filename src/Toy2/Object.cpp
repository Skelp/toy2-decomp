#include "Object.h"
#include "Levels.h"

namespace Toy2
{
	namespace Object
	{
		// FUNCTION: TOY2 0x0044E520 [MATCHED]
		int32_t Classify(int32_t objectIndex)
		{
			switch (Levels::g_objectListBase->entries[objectIndex]->desc->classification)
			{
				case 32:
					return 0;
				case 60:
					return 1;
				case 36:
				case 72:
					return 2;
				case 18:
					return 3;
				case 6:
					return 4;
				case 39:
					return 5;
				case 100:
					return 6;
				case 30:
					return 7;
				case 20:
					return 8;
				case 31:
				case 50:
				case 80:
					return 9;
				case 19:
					return 10;
				default:
					return -1;
			}
		}
	}
}
