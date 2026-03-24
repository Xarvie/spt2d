#include "../Spt3D.h"
#include <random>
#include <unordered_map>

namespace spt3d {

static std::mt19937 g_rng(std::random_device{}());

float Randf() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(g_rng);
}

int Randi(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(g_rng);
}

void SeedRand(unsigned int seed) {
    g_rng.seed(seed);
}

float RandRange(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(g_rng);
}

}
