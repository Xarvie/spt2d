#pragma once

#include "../glad/glad.h"

namespace spt3d {
namespace gfx {

enum class BlendMode {
    None,
    Alpha,
    Additive,
    Multiply,
    Screen,
    Premultiplied
};

inline void applyBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::None:
            glDisable(GL_BLEND);
            break;

        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            break;

        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);
            break;

        case BlendMode::Multiply:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            break;

        case BlendMode::Screen:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            glBlendEquation(GL_FUNC_ADD);
            break;

        case BlendMode::Premultiplied:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            break;
    }
}

} // namespace gfx
} // namespace spt3d
