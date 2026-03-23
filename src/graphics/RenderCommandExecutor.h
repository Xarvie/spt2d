#pragma once

#include "GameWork.h"
#include "../glad/glad.h"
#include <algorithm>

namespace spt {

class RenderStateCache {
public:
    void reset() { *this = RenderStateCache{}; }

    bool setProgram(GLuint program) {
        if (program == currentProgram_) return false;
        currentProgram_ = program;
        glUseProgram(program);
        return true;
    }

    bool setVAO(GLuint vao) {
        if (vao == currentVao_) return false;
        currentVao_ = vao;
        glBindVertexArray(vao);
        return true;
    }

    bool setTexture(GLuint unit, GLenum target, GLuint tex) {
        if (unit < kMaxTextureUnits) {
            auto& slot = textureSlots_[unit];
            if (slot.target == target && slot.id == tex) return false;
            slot = {target, tex};
        }
        if (activeTextureUnit_ != unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            activeTextureUnit_ = unit;
        }
        glBindTexture(target, tex);
        return true;
    }

    bool setBlend(bool enable) {
        if (enable == blendEnabled_) return false;
        blendEnabled_ = enable;
        enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        return true;
    }

    bool setBlendFunc(GLenum src, GLenum dst) {
        if (src == blendSrc_ && dst == blendDst_) return false;
        blendSrc_ = src; blendDst_ = dst;
        glBlendFunc(src, dst);
        return true;
    }

    bool setViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
        if (x == vpX_ && y == vpY_ && w == vpW_ && h == vpH_) return false;
        vpX_ = x; vpY_ = y; vpW_ = w; vpH_ = h;
        glViewport(x, y, w, h);
        return true;
    }

private:
    static constexpr size_t kMaxTextureUnits = 16;
    struct TextureSlot { GLenum target = 0; GLuint id = 0; };

    GLuint currentProgram_ = 0;
    GLuint currentVao_ = 0;
    GLuint activeTextureUnit_ = 0;
    TextureSlot textureSlots_[kMaxTextureUnits] = {};

    bool blendEnabled_ = false;
    GLenum blendSrc_ = GL_ONE;
    GLenum blendDst_ = GL_ZERO;

    GLint vpX_ = 0, vpY_ = 0;
    GLsizei vpW_ = 0, vpH_ = 0;
};

struct ClearCmd {
    GLbitfield mask;
    float color[4];
    float depth;
    int stencil;
};

struct SetViewportCmd {
    GLint x, y;
    GLsizei w, h;
};

namespace RenderCommandExec {

inline void clear(const void* p) {
    auto& c = *static_cast<const ClearCmd*>(p);
    if (c.mask & GL_COLOR_BUFFER_BIT)
        glClearColor(c.color[0], c.color[1], c.color[2], c.color[3]);
    if (c.mask & GL_DEPTH_BUFFER_BIT)
        glClearDepthf(c.depth);
    if (c.mask & GL_STENCIL_BUFFER_BIT)
        glClearStencil(c.stencil);
    glClear(c.mask);
}

inline void setViewport(const void* p) {
    auto& c = *static_cast<const SetViewportCmd*>(p);
    glViewport(c.x, c.y, c.w, c.h);
}

}

class RenderCommandExecutor {
public:
    void execute(const GameWork& work) {
        if (work.hasViewport) {
            glViewport(work.viewportX, work.viewportY, work.viewportW, work.viewportH);
        }

        auto& commands = work.renderCommands;
        
        std::vector<size_t> indices(commands.size());
        for (size_t i = 0; i < commands.size(); ++i) indices[i] = i;
        std::stable_sort(indices.begin(), indices.end(), 
            [&commands](size_t a, size_t b) {
                return commands[a].sortKey < commands[b].sortKey;
            });

        for (size_t idx : indices) {
            commands[idx].execute();
        }
    }

    void reset() {
        stateCache_.reset();
    }

private:
    RenderStateCache stateCache_;
};

inline void buildClearCommand(GameWork& work, GLbitfield mask,
                               float r = 0, float g = 0, float b = 0, float a = 1,
                               float depth = 1.0f, int stencil = 0) {
    ClearCmd cmd;
    cmd.mask = mask;
    cmd.color[0] = r; cmd.color[1] = g;
    cmd.color[2] = b; cmd.color[3] = a;
    cmd.depth = depth;
    cmd.stencil = stencil;
    
    work.renderCommands.push_back(RenderCommand::create(cmd, RenderCommandExec::clear));
}

inline void buildViewportCommand(GameWork& work, GLint x, GLint y, GLsizei w, GLsizei h) {
    SetViewportCmd cmd{x, y, w, h};
    work.renderCommands.push_back(RenderCommand::create(cmd, RenderCommandExec::setViewport));
    work.hasViewport = true;
    work.viewportX = x;
    work.viewportY = y;
    work.viewportW = w;
    work.viewportH = h;
}

}
