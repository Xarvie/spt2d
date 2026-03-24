#include "../Spt3D.h"

#include <vector>

namespace spt3d {

static std::vector<Light> g_lights;
static Vec3 g_ambient = {0.1f, 0.1f, 0.1f};
static float g_ambientIntensity = 1.0f;
static int g_shadowMapSize = 1024;

int AddLight(Light light) {
    int idx = static_cast<int>(g_lights.size());
    g_lights.push_back(light);
    return idx;
}

void SetLight(int idx, Light light) {
    if (idx >= 0 && idx < static_cast<int>(g_lights.size())) {
        g_lights[idx] = light;
    }
}

void RemoveLight(int idx) {
    if (idx >= 0 && idx < static_cast<int>(g_lights.size())) {
        g_lights.erase(g_lights.begin() + idx);
    }
}

void ClearLights() {
    g_lights.clear();
}

int LightCount() {
    return static_cast<int>(g_lights.size());
}

Light GetLight(int idx) {
    if (idx >= 0 && idx < static_cast<int>(g_lights.size())) {
        return g_lights[idx];
    }
    return Light{};
}

void SetAmbient(Vec3 color, float intensity) {
    g_ambient = color;
    g_ambientIntensity = intensity;
}

Vec3 GetAmbientColor() {
    return g_ambient;
}

float GetAmbientIntensity() {
    return g_ambientIntensity;
}

void SetShadowMapSize(int size) {
    g_shadowMapSize = size;
}

int GetShadowMapSize() {
    return g_shadowMapSize;
}

}
