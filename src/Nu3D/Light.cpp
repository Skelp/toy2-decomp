#include "Nu3D/Light.h"
#include "Nu3D/Math.h"
#include "DrawingDevice.h"

#include <FLOAT.H>
#include <MATH.H>
#include <STDLIB.H>

namespace
{
	uint8_t ColorChannelToByte(float channel)
	{
		int32_t value = (int32_t)(channel * 255.0f);

		if (value < 0)
			return 0;
		if (value > 255)
			return 255;

		return (uint8_t)value;
	}
}

namespace Nu3D
{
	// GLOBAL: TOY2 0x00AAD79C
	Light* g_lightPool;

	// GLOBAL: TOY2 0x00AAD7A0
	Light* g_freeLightHead;

	// GLOBAL: TOY2 0x00AAD7A4
	Light* g_allocatedLightTail;

	// GLOBAL: TOY2 0x00B62418
	Light* g_defaultDirectionalLight1;

	// GLOBAL: TOY2 0x00B6241C
	Light* g_defaultDirectionalLight2;

	// GLOBAL: TOY2 0x00B62420
	Light* g_defaultDirectionalLight3;

	// GLOBAL: TOY2 0x00B62424
	Light* g_directionalLight;

	// GLOBAL: TOY2 0x00B62428
	Light* g_ambientLight;

	// FUNCTION: TOY2 0x004C29A0 [MATCHED]
	void Light::Destroy(Light* light)
	{
		if (!light)
			return;

		if (light->direct3DLight)
		{
			DrawingDevice::DeleteLight(light->direct3DLight);
			DrawingDevice::ReleaseLight(light->direct3DLight);
		}

		Free(light);
	}

	// FUNCTION: TOY2 0x004C29D0 [MATCHED]
	void Light::Free(Light* light)
	{
		if (light->previous)
			light->previous->next = light->next;

		if (light->next)
		{
			light->next->previous = light->previous;
		}
		else
		{
			g_allocatedLightTail = light->previous;
		}

		light->previous = g_freeLightHead;
		g_freeLightHead = light;
	}

	// FUNCTION: TOY2 0x004C2A20 [MATCHED]
	Light* Light::BuildAmbient(const LightColor* color, int32_t enabled)
	{
		Light* light = Alloc();

		if (light)
		{
			light->type = TYPE_AMBIENT;
			light->color = *color;
			light->enabled = enabled != 0;
			Update(light);
		}

		return light;
	}

	// FUNCTION: TOY2 0x004C2A70
	Light* Light::Alloc()
	{
		Light* light = g_freeLightHead;

		if (light)
		{
			g_freeLightHead = light->previous;
			light->previous = g_allocatedLightTail;

			if (g_allocatedLightTail)
				g_allocatedLightTail->next = light;

			light->next = 0;
			g_allocatedLightTail = light;
			light->direct3DLight = 0;
		}

		return light;
	}

	// FUNCTION: TOY2 0x004C2AB0
	Light* Light::BuildPoint(const D3DMATRIX* transform, const LightColor* color, float range, int32_t enabled)
	{
		Light* light = Alloc();

		if (!light)
			return 0;

		if (DrawingDevice::CreateLight(&light->direct3DLight) != 0)
		{
			Free(light);
			return 0;
		}

		light->transform = *transform;
		light->color = *color;
		light->type = TYPE_POINT;
		light->range = range;
		light->theta = 0x8000;
		light->phi = 0x8000;
		light->enabled = enabled != 0;

		D3DLIGHT2& description = light->direct3DDescription;
		description.dwSize = sizeof(description);
		description.dltType = D3DLIGHT_POINT;
		description.dcvColor.r = light->color.r;
		description.dcvColor.g = light->color.g;
		description.dcvColor.b = light->color.b;
		description.dcvColor.a = light->color.a;
		Math::GetPositionVector(&light->transform, (Vector3F*)&description.dvPosition);
		Math::GetForwardVector(&light->transform, (Vector3F*)&description.dvDirection);
		description.dvRange = light->range;
		description.dvFalloff = 1.0f;
		description.dvAttenuation0 = 1.0f;
		description.dvAttenuation1 = 1.0f;
		description.dvAttenuation2 = 1.0f;
		description.dvTheta = 0.0f;
		description.dvPhi = 0.0f;
		description.dwFlags = light->enabled;

		DrawingDevice::SetLight(light->direct3DLight, &description);
		DrawingDevice::AddLight(light->direct3DLight);
		return light;
	}

	// FUNCTION: TOY2 0x004C2BE0
	Light* Light::BuildDirectional(const D3DMATRIX* transform, const LightColor* color, int32_t enabled)
	{
		Light* light = Alloc();

		if (!light)
			return 0;

		if (DrawingDevice::CreateLight(&light->direct3DLight) != 0)
		{
			Free(light);
			return 0;
		}

		light->transform = *transform;
		light->color = *color;
		light->type = TYPE_DIRECTIONAL;
		light->range = (float)sqrt((double)FLT_MAX);
		light->theta = 0x8000;
		light->phi = 0x8000;
		light->enabled = enabled != 0;

		D3DLIGHT2& description = light->direct3DDescription;
		description.dwSize = sizeof(description);
		description.dltType = D3DLIGHT_DIRECTIONAL;
		description.dcvColor.r = light->color.r;
		description.dcvColor.g = light->color.g;
		description.dcvColor.b = light->color.b;
		description.dcvColor.a = light->color.a;
		Math::GetPositionVector(&light->transform, (Vector3F*)&description.dvPosition);
		Math::GetForwardVector(&light->transform, (Vector3F*)&description.dvDirection);
		description.dvRange = light->range;
		description.dvFalloff = 1.0f;
		description.dvAttenuation0 = 1.0f;
		description.dvAttenuation1 = 1.0f;
		description.dvAttenuation2 = 1.0f;
		description.dvTheta = 0.0f;
		description.dvPhi = 0.0f;
		description.dwFlags = light->enabled;

		DrawingDevice::SetLight(light->direct3DLight, &description);
		DrawingDevice::AddLight(light->direct3DLight);
		return light;
	}

	// FUNCTION: TOY2 0x004C2D20
	Light* Light::BuildSpot(const D3DMATRIX* transform, const LightColor* color, float range,
		int32_t theta, int32_t phi, int32_t enabled)
	{
		Light* light = Alloc();

		if (!light)
			return 0;

		if (DrawingDevice::CreateLight(&light->direct3DLight) != 0)
		{
			Free(light);
			return 0;
		}

		light->transform = *transform;
		light->color = *color;
		light->type = TYPE_SPOT;
		light->range = range;
		light->theta = theta;
		light->phi = phi;
		light->enabled = enabled != 0;

		D3DLIGHT2& description = light->direct3DDescription;
		description.dwSize = sizeof(description);
		description.dltType = D3DLIGHT_SPOT;
		description.dcvColor.r = light->color.r;
		description.dcvColor.g = light->color.g;
		description.dcvColor.b = light->color.b;
		description.dcvColor.a = light->color.a;
		Math::GetPositionVector(&light->transform, (Vector3F*)&description.dvPosition);
		Math::GetForwardVector(&light->transform, (Vector3F*)&description.dvDirection);
		description.dvRange = light->range;
		description.dvFalloff = 1.0f;
		description.dvAttenuation0 = 1.0f;
		description.dvAttenuation1 = 1.0f;
		description.dvAttenuation2 = 1.0f;
		description.dvTheta = (float)light->theta * 3.14159265358979323846f / 32768.0f;
		description.dvPhi = (float)light->phi * 3.14159265358979323846f / 32768.0f;
		description.dwFlags = light->enabled;

		DrawingDevice::SetLight(light->direct3DLight, &description);
		DrawingDevice::AddLight(light->direct3DLight);
		return light;
	}

	// FUNCTION: TOY2 0x004C2E70
	void Light::Update(Light* light)
	{
		if (light->type != TYPE_AMBIENT)
		{
			D3DLIGHT2& description = light->direct3DDescription;

			Math::GetPositionVector(&light->transform, (Vector3F*)&description.dvPosition);
			Math::GetForwardVector(&light->transform, (Vector3F*)&description.dvDirection);
			description.dcvColor.r = light->color.r;
			description.dcvColor.g = light->color.g;
			description.dcvColor.b = light->color.b;
			description.dcvColor.a = light->color.a;
			description.dvRange = light->range;
			description.dvTheta = (float)light->theta * 3.14159265358979323846f / 32768.0f;
			description.dvPhi = (float)light->phi * 3.14159265358979323846f / 32768.0f;
			description.dwFlags = light->enabled & D3DLIGHT_ACTIVE;
			DrawingDevice::SetLight(light->direct3DLight, &description);
		}
		else if (light->enabled & D3DLIGHT_ACTIVE)
		{
			RGBA ambientColor;
			ambientColor.b = ColorChannelToByte(light->color.b);
			ambientColor.g = ColorChannelToByte(light->color.g);
			ambientColor.r = ColorChannelToByte(light->color.r);
			ambientColor.a = ColorChannelToByte(light->color.a);
			DrawingDevice::SetLightState(D3DLIGHTSTATE_AMBIENT, ambientColor.value);
		}
	}

	// FUNCTION: TOY2 0x004C2FF0
	int32_t Light::InitPool(int32_t poolSize)
	{
		if (g_lightPool)
			DestroyAllLights();

		g_lightPool = (Light*)malloc(sizeof(Light) * poolSize);
		if (!g_lightPool)
			return 0;

		for (int32_t i = 0; i < poolSize; ++i)
		{
			g_lightPool[i].previous = i + 1 < poolSize ? &g_lightPool[i + 1] : 0;
			g_lightPool[i].next = i > 0 ? &g_lightPool[i - 1] : 0;
		}

		g_freeLightHead = g_lightPool;
		g_allocatedLightTail = 0;
		return 1;
	}

	// FUNCTION: TOY2 0x004C3090 [MATCHED]
	void Light::DestroyAllLights()
	{
		if (!g_lightPool)
			return;

		while (g_allocatedLightTail)
			Destroy(g_allocatedLightTail);

		free(g_lightPool);
		g_lightPool = 0;
	}

	// FUNCTION: TOY2 0x004CE6C0 [MATCHED]
	void Light::SetDirectionalLight(int32_t directionX, int32_t directionY, int32_t directionZ,
		int32_t red, int32_t green, int32_t blue)
	{
		if (directionX == 0 && directionY == 0 && directionZ == 0)
		{
			Math::BuildIdentityMatrix(&g_directionalLight->transform);
		}
		else
		{
			Vector3F direction;
			direction.x = (float)directionX;
			direction.y = (float)directionY;
			direction.z = (float)directionZ;
			Math::BuildMatrixFromDirection(&g_directionalLight->transform, &direction);
		}

		const float colorScale = 1.0f / 128.0f;
		g_directionalLight->color.r = (float)red * colorScale;
		g_directionalLight->color.g = (float)green * colorScale;
		g_directionalLight->color.b = (float)blue * colorScale;
		Update(g_directionalLight);
	}

	// FUNCTION: TOY2 0x004CE910
	void Light::BuildGlobalLights()
	{
		InitPool(8);

		LightColor ambientColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		g_ambientLight = BuildAmbient(&ambientColor, 1);

		LightColor white = { 1.0f, 1.0f, 1.0f, 1.0f };
		D3DMATRIX firstTransform;
		D3DMATRIX secondTransform;
		D3DMATRIX thirdTransform;

		Math::SetRotationYFromU16AngleLUT(&firstTransform, 0x0000);
		g_defaultDirectionalLight1 = BuildDirectional(&firstTransform, &white, 1);
		Math::SetRotationYFromU16AngleLUT(&secondTransform, 0x5555);
		g_defaultDirectionalLight2 = BuildDirectional(&secondTransform, &white, 1);
		Math::SetRotationYFromU16AngleLUT(&thirdTransform, 0xAAAA);
		g_defaultDirectionalLight3 = BuildDirectional(&thirdTransform, &white, 1);
		g_directionalLight = BuildDirectional(&firstTransform, &white, 1);
	}

	// FUNCTION: TOY2 0x004CEA50
	void Light::Cleanup()
	{
		Destroy(g_defaultDirectionalLight1);
		Destroy(g_defaultDirectionalLight2);
		Destroy(g_defaultDirectionalLight3);
		Destroy(g_directionalLight);
		Destroy(g_ambientLight);
		DestroyAllLights();

		g_ambientLight = 0;
		g_directionalLight = 0;
		g_defaultDirectionalLight3 = 0;
		g_defaultDirectionalLight2 = 0;
		g_defaultDirectionalLight1 = 0;
	}
}
