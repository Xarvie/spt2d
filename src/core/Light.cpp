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

void SetAmbient(Color color, float intensity) {
    g_ambient = Vec3(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);
    g_ambientIntensity = intensity;
}

Color GetAmbientColor() {
    return Color{
        static_cast<uint8_t>(g_ambient.r * 255.0f),
        static_cast<uint8_t>(g_ambient.g * 255.0f),
        static_cast<uint8_t>(g_ambient.b * 255.0f),
        255
    };
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

static Texture g_skybox;
static Texture g_envMap;
static bool g_fogEnabled = false;
static Color g_fogColor = {128, 128, 128, 255};
static float g_fogStart = 10.0f;
static float g_fogEnd = 100.0f;
static float g_fogDensity = 0.01f;
static bool g_fogExp = false;

void SetSkybox(Texture cubemap) {
    g_skybox = std::move(cubemap);
}

void SetEnvMap(Texture cubemap) {
    g_envMap = std::move(cubemap);
}

Texture GetSkybox() {
    return g_skybox;
}

Texture GetEnvMap() {
    return g_envMap;
}

void SetFog(Color color, float start, float end) {
    g_fogEnabled = true;
    g_fogColor = color;
    g_fogStart = start;
    g_fogEnd = end;
    g_fogExp = false;
}

void SetFogExp(Color color, float density) {
    g_fogEnabled = true;
    g_fogColor = color;
    g_fogDensity = density;
    g_fogExp = true;
}

void DisableFog() {
    g_fogEnabled = false;
}

bool IsFogEnabled() {
    return g_fogEnabled;
}

Color GetFogColor() {
    return g_fogColor;
}

float GetFogStart() {
    return g_fogStart;
}

float GetFogEnd() {
    return g_fogEnd;
}

float GetFogDensity() {
    return g_fogDensity;
}

bool IsFogExp() {
    return g_fogExp;
}

}
