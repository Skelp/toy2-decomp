#include "Common.h"
#include "Logger.h"
#include <cstring>

namespace EventManager
{
	typedef void (*EventCallback)();

	struct EventSlot
	{
		int16_t index;
		int16_t flags;
		int32_t userData;
		EventCallback updateCallback;
		EventCallback destroyCallback;
		char name[32];
		int16_t state;
		int16_t timer;
		int32_t active;
		int32_t pending;
	};
	STATIC_ASSERT(sizeof(EventSlot) == 0x3C);

	// GLOBAL: TOY2 0x00529390
	EventSlot g_eventSlots[16];

	// GLOBAL: TOY2 0x00529750
	char g_eventCallbackName[128];

	// FUNCTION: TOY2 0x00413E80 [PROVISIONAL]
	void DeleteAll()
	{
		EventSlot* event = &g_eventSlots[15];
		int32_t count = 16;
		do
		{
			if (event->active)
			{
				if (event->destroyCallback)
				{
					memset(g_eventCallbackName, 0, sizeof(g_eventCallbackName));
					Logger::Log("EVENT : Event >>>%s<<< destroy function %s called.\n", event->name, g_eventCallbackName);
					event->destroyCallback();
				}

				event->active = 0;
				event->pending = 0;
				event->state = 0;
				event->flags = 0;
				event->timer = 0;
				event->userData = 0;
				event->updateCallback = 0;
				event->destroyCallback = 0;
				Logger::Log("EVENT : Event >>>%s<<< deleted.\n", event->name);
			}

			--event;
		} while (--count);
	}
}
