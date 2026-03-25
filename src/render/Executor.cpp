#include "Executor.h"
#include "../glad/glad.h"
#include "../resource/MaterialSnapshot.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace spt3d {

namespace {

GPUDevice* g_gpuDevice = nullptr;
const PerFrameData* g_frameData = nullptr;

}

void RenderStateCache::reset() noexcept {
    m_program = 0;
    m_vao = 0;
    m_blend = false;
    m_depthTest = false;
    m_depthWrite = true;
}

bool RenderStateCache::setProgram(uint32_t program) noexcept {
    if (m_program == program) return false;
    m_program = program;
    return true;
}

bool RenderStateCache::setVAO(uint32_t vao) noexcept {
    if (m_vao == vao) return false;
    m_vao = vao;
    return true;
}

bool RenderStateCache::setBlend(bool enable) noexcept {
    if (m_blend == enable) return false;
    m_blend = enable;
    return true;
}

bool RenderStateCache::setDepthTest(bool enable) noexcept {
    if (m_depthTest == enable) return false;
    m_depthTest = enable;
    return true;
}

bool RenderStateCache::setDepthWrite(bool enable) noexcept {
    if (m_depthWrite == enable) return false;
    m_depthWrite = enable;
    return true;
}

Executor::Executor(std::size_t reserveCapacity) {
    m_sortIndices.reserve(reserveCapacity);
}

void Executor::execute(const GameWork& work, GPUDevice& gpu) {
    g_gpuDevice = &gpu;
    g_frameData = &work.frameData;

    gpu.processResourceCommands(work);

    const uint8_t* poolBase = work.uniformPool.data();

    if (work.viewport.valid) {
        glViewport(work.viewport.x, work.viewport.y, work.viewport.w, work.viewport.h);
    }

    const std::size_t cmdCount = work.renderCommands.size();
    if (cmdCount == 0) {
        g_gpuDevice = nullptr;
        g_frameData = nullptr;
        return;
    }

    m_sortIndices.resize(cmdCount);
    for (std::size_t i = 0; i < cmdCount; ++i) {
        m_sortIndices[i] = i;
    }

    std::stable_sort(m_sortIndices.begin(), m_sortIndices.end(),
        [&work](std::size_t a, std::size_t b) {
            return work.renderCommands[a].sortKey < work.renderCommands[b].sortKey;
        });

    for (std::size_t idx : m_sortIndices) {
        const RenderCommand& cmd = work.renderCommands[idx];
        cmd.execute(poolBase);
    }

    g_gpuDevice = nullptr;
    g_frameData = nullptr;
}

void Executor::resetStateCache() noexcept {
    m_stateCache.reset();
}

namespace cmd {

void execDrawMesh(const void* payload, const uint8_t* poolBase) noexcept {
    if (!g_gpuDevice) return;
    GPUDevice& gpu = *g_gpuDevice;

    const DrawMeshCmd* cmd = static_cast<const DrawMeshCmd*>(payload);

    gpu.bindMesh(cmd->mesh);
    gpu.useShader(cmd->shader);

    if (g_frameData) {
        gpu.setUniformMat4(cmd->shader, "u_view", g_frameData->view);
        gpu.setUniformMat4(cmd->shader, "u_proj", g_frameData->projection);
    }

    if (cmd->uniformsOffset != UINT32_MAX) {
        const MaterialUniforms* uniforms = reinterpret_cast<const MaterialUniforms*>(
            poolBase + cmd->uniformsOffset);
        gpu.applyMaterialUniforms(cmd->shader, *uniforms);
    }

    if (cmd->transformOffset != UINT32_MAX) {
        const Mat4* model = reinterpret_cast<const Mat4*>(poolBase + cmd->transformOffset);
        gpu.setUniformMat4(cmd->shader, "u_model", *model);
    }

    if (gpu.meshIsIndexed(cmd->mesh)) {
        gpu.drawIndexed(0);
    } else {
        gpu.drawArrays(0);
    }
}

void execClear(const void* payload, const uint8_t* poolBase) noexcept {
    (void)poolBase;
    const ClearCmd* cmd = static_cast<const ClearCmd*>(payload);

    if (cmd->mask & GL_COLOR_BUFFER_BIT) {
        glClearColor(cmd->color[0], cmd->color[1], cmd->color[2], cmd->color[3]);
    }
    if (cmd->mask & GL_DEPTH_BUFFER_BIT) {
        glClearDepthf(cmd->depth);
        glDepthMask(GL_TRUE);
    }
    if (cmd->mask & GL_STENCIL_BUFFER_BIT) {
        glClearStencil(cmd->stencil);
    }

    glClear(cmd->mask);
}

void execSetViewport(const void* payload, const uint8_t* poolBase) noexcept {
    (void)poolBase;
    const SetViewportCmd* cmd = static_cast<const SetViewportCmd*>(payload);
    glViewport(cmd->x, cmd->y, cmd->w, cmd->h);
}

void execBindRT(const void* payload, const uint8_t* poolBase) noexcept {
    (void)poolBase;
    if (!g_gpuDevice) return;
    GPUDevice& gpu = *g_gpuDevice;

    const BindRTCmd* cmd = static_cast<const BindRTCmd*>(payload);

    if (cmd->rt.value == 0) {
        gpu.bindDefaultFramebuffer();
    } else {
        gpu.bindRenderTarget(cmd->rt);
    }
}

void execBlit(const void* payload, const uint8_t* poolBase) noexcept {
    if (!g_gpuDevice) return;
    GPUDevice& gpu = *g_gpuDevice;

    const BlitCmd* cmd = static_cast<const BlitCmd*>(payload);

    if (cmd->target.value == 0) {
        gpu.bindDefaultFramebuffer();
    } else {
        gpu.bindRenderTarget(cmd->target);
    }

    gpu.useShader(cmd->shader);

    for (int i = 0; i < cmd->inputCount && i < 4; ++i) {
        gpu.bindTexture(cmd->inputs[i], i);
    }

    if (cmd->uniformsOffset != UINT32_MAX) {
        const MaterialUniforms* uniforms = reinterpret_cast<const MaterialUniforms*>(
            poolBase + cmd->uniformsOffset);
        gpu.applyMaterialUniforms(cmd->shader, *uniforms);
    }

    RenderState rs;
    rs.blend = BlendMode::Disabled;
    rs.depth_test = false;
    rs.depth_write = false;
    rs.cull = CullFace::None;
    gpu.applyRenderState(rs);

    gpu.drawFullscreenTriangle();
}

} // namespace cmd

void buildClearCommand(GameWork& work, uint32_t mask,
                       float r, float g, float b, float a,
                       float depth, int stencil,
                       uint64_t sortKey) {
    ClearCmd cmd;
    cmd.mask = mask;
    cmd.color[0] = r;
    cmd.color[1] = g;
    cmd.color[2] = b;
    cmd.color[3] = a;
    cmd.depth = depth;
    cmd.stencil = stencil;

    work.submit(cmd, cmd::execClear, sortKey, ClearCmd::kTypeId);
}

void buildViewportCommand(GameWork& work, int x, int y, int w, int h,
                          uint64_t sortKey) {
    SetViewportCmd cmd;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;

    work.submit(cmd, cmd::execSetViewport, sortKey, SetViewportCmd::kTypeId);
}

void buildBindRTCommand(GameWork& work, RTHandle rt, uint64_t sortKey) {
    BindRTCmd cmd;
    cmd.rt = rt;

    work.submit(cmd, cmd::execBindRT, sortKey, BindRTCmd::kTypeId);
}

void buildBlitCommand(GameWork& work, ShaderHandle shader,
                      const TexHandle* inputs, int inputCount,
                      RTHandle target,
                      const MaterialSnapshot* matSnap,
                      uint64_t sortKey) {
    BlitCmd cmd;
    cmd.shader = shader;
    cmd.inputCount = std::min(inputCount, 4);
    for (int i = 0; i < cmd.inputCount; ++i) {
        cmd.inputs[i] = inputs[i];
    }
    cmd.target = target;

    if (matSnap) {
        MaterialUniforms mu;
        mu.texCount = std::min(matSnap->texCount, MaterialUniforms::kMaxTextures);
        for (int i = 0; i < mu.texCount; ++i) {
            mu.textures[i].tex = matSnap->textures[i].tex;
            mu.textures[i].nameHash = matSnap->textures[i].nameHash;
        }
        mu.floatCount = std::min(matSnap->floatCount, MaterialUniforms::kMaxFloats);
        for (int i = 0; i < mu.floatCount; ++i) {
            mu.floats[i].nameHash = matSnap->floats[i].nameHash;
            mu.floats[i].value = matSnap->floats[i].value;
        }
        mu.vec4Count = std::min(matSnap->vec4Count, MaterialUniforms::kMaxVec4s);
        for (int i = 0; i < mu.vec4Count; ++i) {
            mu.vec4s[i].nameHash = matSnap->vec4s[i].nameHash;
            mu.vec4s[i].value = matSnap->vec4s[i].value;
        }
        mu.mat4Count = std::min(matSnap->mat4Count, MaterialUniforms::kMaxMat4s);
        for (int i = 0; i < mu.mat4Count; ++i) {
            mu.mat4s[i].nameHash = matSnap->mat4s[i].nameHash;
            mu.mat4s[i].value = matSnap->mat4s[i].value;
        }
        mu.state = matSnap->state;
        cmd.uniformsOffset = work.allocUniform(mu);
    } else {
        cmd.uniformsOffset = UINT32_MAX;
    }

    work.submit(cmd, cmd::execBlit, sortKey, BlitCmd::kTypeId);
}

} // namespace spt3d
