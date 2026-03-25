#pragma once

#include "../core/GameWork.h"
#include "../glad/glad.h"
#include "Graphics.h"

#include <vector>
#include <numeric>
#include <algorithm>
#include <cstddef>

namespace spt3d {

// =============================================================================
//  RenderStateCache
//
//  Tracks the currently-bound GL objects and avoids redundant state changes.
//  Call reset() at the start of each frame (or whenever GL state may have
//  been changed by external code) to force re-binding on the next use.
//
//  All methods return true if the GL call was actually issued, false if the
//  state was already correct (useful for profiling/debug counters).
// =============================================================================
class RenderStateCache {
public:
    void reset() noexcept { *this = RenderStateCache{}; }

    bool setProgram(GLuint program) noexcept {
        if (program == m_program) return false;
        m_program = program;
        glUseProgram(program);
        return true;
    }

    bool setVAO(GLuint vao) noexcept {
        if (vao == m_vao) return false;
        m_vao = vao;
        glBindVertexArray(vao);
        return true;
    }

    bool setTexture(GLuint unit, GLenum target, GLuint tex) noexcept {
        if (unit < kMaxTextureUnits) {
            auto& slot = m_texSlots[unit];
            if (slot.target == target && slot.id == tex) return false;
            slot = {target, tex};
        }
        if (m_activeTexUnit != unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            m_activeTexUnit = unit;
        }
        glBindTexture(target, tex);
        return true;
    }

    bool setBlend(bool enable) noexcept {
        if (enable == m_blendEnabled) return false;
        m_blendEnabled = enable;
        enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        return true;
    }

    bool setBlendFunc(GLenum src, GLenum dst) noexcept {
        if (src == m_blendSrc && dst == m_blendDst) return false;
        m_blendSrc = src;
        m_blendDst = dst;
        glBlendFunc(src, dst);
        return true;
    }

    bool setDepthTest(bool enable) noexcept {
        if (enable == m_depthTest) return false;
        m_depthTest = enable;
        enable ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        return true;
    }

    bool setDepthWrite(bool enable) noexcept {
        if (enable == m_depthWrite) return false;
        m_depthWrite = enable;
        glDepthMask(enable ? GL_TRUE : GL_FALSE);
        return true;
    }

    bool setViewport(GLint x, GLint y, GLsizei w, GLsizei h) noexcept {
        if (x == m_vpX && y == m_vpY && w == m_vpW && h == m_vpH) return false;
        m_vpX = x; m_vpY = y; m_vpW = w; m_vpH = h;
        glViewport(x, y, w, h);
        return true;
    }

private:
    static constexpr std::size_t kMaxTextureUnits = 16;
    struct TexSlot { GLenum target = 0; GLuint id = 0; };

    GLuint  m_program      = 0;
    GLuint  m_vao          = 0;
    GLuint  m_activeTexUnit = 0;
    TexSlot m_texSlots[kMaxTextureUnits] = {};

    bool   m_blendEnabled = false;
    GLenum m_blendSrc     = GL_ONE;
    GLenum m_blendDst     = GL_ZERO;
    bool   m_depthTest    = false;
    bool   m_depthWrite   = true;

    GLint   m_vpX = 0, m_vpY = 0;
    GLsizei m_vpW = 0, m_vpH = 0;
};


// =============================================================================
//  Built-in command payload types
//
//  These are trivially-copyable structs that fit within
//  RenderCommand::kPayloadSize (64 bytes) and are executed by a plain
//  function pointer – no std::function, no heap allocation.
// =============================================================================

struct ClearCmd {
    GLbitfield mask      = 0;
    float      color[4]  = {0, 0, 0, 1};
    float      depth     = 1.0f;
    int        stencil   = 0;
};
static_assert(sizeof(ClearCmd) <= RenderCommand::kPayloadSize);
static_assert(std::is_trivially_copyable<ClearCmd>::value);

struct SetViewportCmd {
    GLint   x = 0, y = 0;
    GLsizei w = 0, h = 0;
};
static_assert(sizeof(SetViewportCmd) <= RenderCommand::kPayloadSize);
static_assert(std::is_trivially_copyable<SetViewportCmd>::value);


// =============================================================================
//  Built-in execute functions
// =============================================================================
namespace RenderCommandExec {

inline void clear(const void* p) noexcept {
    const auto& c = *static_cast<const ClearCmd*>(p);
    if (c.mask & GL_COLOR_BUFFER_BIT)
        glClearColor(c.color[0], c.color[1], c.color[2], c.color[3]);
    if (c.mask & GL_DEPTH_BUFFER_BIT)
        glClearDepthf(c.depth);
    if (c.mask & GL_STENCIL_BUFFER_BIT)
        glClearStencil(c.stencil);
    glClear(c.mask);
}

inline void setViewport(const void* p) noexcept {
    const auto& c = *static_cast<const SetViewportCmd*>(p);
    glViewport(c.x, c.y, c.w, c.h);
}

} // namespace RenderCommandExec


// =============================================================================
//  RenderCommandExecutor
//
//  Sorts and dispatches the RenderCommand list recorded in a GameWork frame.
//
//  Performance notes
//  ──────────────────
//  The sort-index vector is a member of the executor and retains its capacity
//  across frames; after the first frame with N commands it will not allocate
//  again until N grows.  This gives zero heap allocation in steady state.
//
//  Sort stability ensures that commands with equal sort keys are dispatched
//  in submission order (preserving the logic thread's intended draw order
//  within the same material bucket).
// =============================================================================
class RenderCommandExecutor {
public:
    explicit RenderCommandExecutor(std::size_t reserveCapacity = 256) {
        m_sortIndices.reserve(reserveCapacity);
    }

    /// Execute all render commands in `work`, sorted by sortKey.
    /// Must be called on the render thread (GL context must be current).
    void execute(const GameWork& work) {
        // Apply the per-frame viewport override if one was set.
        if (work.viewport.valid) {
            glViewport(work.viewport.x, work.viewport.y,
                       work.viewport.w, work.viewport.h);
        }

        auto& cmds = const_cast<std::vector<RenderCommand>&>(work.renderCommands);
        if (cmds.empty()) return;

        // --- Patch poolBase pointers ---
        // During recording, the LinearAllocator may have reallocated its
        // internal buffer, invalidating poolBase pointers stored in earlier
        // commands.  Now that recording is finished, the buffer is stable.
        // We overwrite every command's poolBase with the final base address.
        const uint8_t* finalBase = work.uniformPool.data();
        for (auto& cmd : cmds) {
            if (cmd.typeId == DrawBatchCmd::kTypeId) {
                auto* p = reinterpret_cast<DrawBatchCmd*>(cmd.payload);
                p->poolBase = finalBase;
            } else if (cmd.typeId == DrawTextureCmd::kTypeId) {
                auto* p = reinterpret_cast<DrawTextureCmd*>(cmd.payload);
                p->poolBase = finalBase;
            }
        }

        // Build a sort-index array.  Reusing the member vector avoids a heap
        // allocation every frame once the high-water capacity is reached.
        m_sortIndices.resize(cmds.size());
        std::iota(m_sortIndices.begin(), m_sortIndices.end(), std::size_t{0});
        std::stable_sort(m_sortIndices.begin(), m_sortIndices.end(),
            [&cmds](std::size_t a, std::size_t b) noexcept {
                return cmds[a].sortKey < cmds[b].sortKey;
            });

        for (std::size_t idx : m_sortIndices) {
            cmds[idx].execute();
        }
    }

    /// Reset driver-side state tracking.  Call at frame start if external
    /// code may have changed GL state since the last execute() call.
    void resetStateCache() noexcept { m_stateCache.reset(); }

    RenderStateCache& stateCache() noexcept { return m_stateCache; }

private:
    RenderStateCache         m_stateCache;
    std::vector<std::size_t> m_sortIndices;   // reusable; retains capacity
};


// =============================================================================
//  Convenience command builders
//
//  These free functions create fully-formed RenderCommands and append them to
//  the work's render command list.  They are the preferred way to emit clear
//  and viewport commands from IGameLogic::onRender().
// =============================================================================

inline void buildClearCommand(GameWork& work,
                               GLbitfield mask,
                               float r = 0.0f, float g = 0.0f,
                               float b = 0.0f, float a = 1.0f,
                               float depth   = 1.0f,
                               int   stencil = 0) {
    ClearCmd cmd;
    cmd.mask      = mask;
    cmd.color[0]  = r; cmd.color[1] = g;
    cmd.color[2]  = b; cmd.color[3] = a;
    cmd.depth     = depth;
    cmd.stencil   = stencil;
    work.renderCommands.push_back(
        RenderCommand::create(cmd, RenderCommandExec::clear));
}

inline void buildViewportCommand(GameWork& work,
                                  GLint x, GLint y,
                                  GLsizei w, GLsizei h) {
    // Record a RenderCommand that issues the glViewport call in sorted order.
    SetViewportCmd cmd{x, y, w, h};
    work.renderCommands.push_back(
        RenderCommand::create(cmd, RenderCommandExec::setViewport));

    // Also update the frame-level viewport so RenderCommandExecutor can
    // apply it before any commands run (useful for scissor / state init).
    work.setViewport(x, y, static_cast<int>(w), static_cast<int>(h));
}

} // namespace spt3d
