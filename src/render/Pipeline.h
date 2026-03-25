// spt3d/render/Pipeline.h — Multi-stage render pipeline
// [THREAD: logic→configure, render→execute]
//
// A Pipeline is a list of StageDesc entries. During onRender(), the logic
// thread calls pipeline helper functions that emit the right sequence of
// RenderCommands into GameWork. The render thread just executes them in
// sorted order — it doesn't know about "stages".
//
// This means Pipeline is a LOGIC-SIDE concept: it's a recipe for
// generating commands, not a render-side execution graph.
//
// Usage:
//   // Setup (once)
//   Pipeline pipe;
//   pipe.pushStage(StageDesc::geometry("opaque", "FORWARD", SortMode::FrontToBack));
//   pipe.pushStage(StageDesc::blit("tonemap", toneMapShader, hdrTex));
//
//   // Per frame (in onRender):
//   pipe.emit(work, drawList, cam);
#pragma once

#include "../Types.h"
#include "../Handle.h"
#include "Executor.h"
#include "../resource/DrawList.h"
#include "../resource/MaterialSnapshot.h"

#include <vector>
#include <string>
#include <functional>
#include <optional>

namespace spt3d {

// =========================================================================
//  OutputBinding — stage output → sampler name for subsequent stages
// =========================================================================

struct OutputBinding {
    std::string samplerName;
    int attachmentIndex = 0;
};

// =========================================================================
//  StageDesc — describes one render stage
// =========================================================================

struct StageDesc {
    std::string name;
    StageType   type = StageType::Geometry;

    // ── Target ──────────────────────────────────────────────────────────
    RTHandle    target;                     // kNullRT = screen

    // ── Clear ───────────────────────────────────────────────────────────
    bool  clearColor       = false;
    bool  clearDepth       = false;
    Color clearColorValue  = Colors::Black;
    float clearDepthValue  = 1.0f;

    // ── Geometry stage ──────────────────────────────────────────────────
    std::string passName;                   // shader pass to use
    uint32_t    tagInclude = 0;             // hashed tag (0 = all)
    uint32_t    tagExclude = 0;
    SortMode    sortMode   = SortMode::FrontToBack;
    uint8_t     layer      = 128;           // sort key layer
    std::optional<RenderState> stateOverride;

    // ── Blit stage ──────────────────────────────────────────────────────
    ShaderHandle       blitShader;
    MaterialSnapshot   blitUniforms;
    std::vector<TexHandle> blitInputs;

    // ── Custom stage ────────────────────────────────────────────────────
    std::function<void(GameWork& work)> customFn;

    // ── Output bindings ─────────────────────────────────────────────────
    std::vector<OutputBinding> outputs;

    // ── Named constructors ──────────────────────────────────────────────

    static StageDesc clear(std::string name, RTHandle rt,
                           Color color = Colors::Black, float depth = 1.0f) {
        StageDesc s;
        s.name = std::move(name);
        s.type = StageType::Clear;
        s.target = rt;
        s.clearColor = true;
        s.clearDepth = true;
        s.clearColorValue = color;
        s.clearDepthValue = depth;
        return s;
    }

    static StageDesc geometry(std::string name, std::string pass,
                              SortMode sort = SortMode::FrontToBack,
                              RTHandle rt = kNullRT) {
        StageDesc s;
        s.name = std::move(name);
        s.type = StageType::Geometry;
        s.passName = std::move(pass);
        s.sortMode = sort;
        s.target = rt;
        return s;
    }

    static StageDesc blit(std::string name, ShaderHandle shader,
                          RTHandle rt = kNullRT) {
        StageDesc s;
        s.name = std::move(name);
        s.type = StageType::Blit;
        s.blitShader = shader;
        s.target = rt;
        return s;
    }

    static StageDesc custom(std::string name,
                            std::function<void(GameWork&)> fn) {
        StageDesc s;
        s.name = std::move(name);
        s.type = StageType::Custom;
        s.customFn = std::move(fn);
        return s;
    }

    // Helpers
    StageDesc& addBlitInput(TexHandle tex) { blitInputs.push_back(tex); return *this; }
    StageDesc& addOutput(std::string sampler, int attachment = 0) {
        outputs.push_back({std::move(sampler), attachment});
        return *this;
    }
    StageDesc& withTag(std::string_view include, std::string_view exclude = "") {
        tagInclude = include.empty() ? 0 : static_cast<uint32_t>(HashName(include));
        tagExclude = exclude.empty() ? 0 : static_cast<uint32_t>(HashName(exclude));
        return *this;
    }
};

// =========================================================================
//  Pipeline
// =========================================================================

class Pipeline {
public:
    Pipeline() = default;

    void pushStage(StageDesc stage);
    void insertStage(int index, StageDesc stage);
    void removeStage(int index);
    void removeStage(std::string_view name);
    void enableStage(std::string_view name, bool on);

    [[nodiscard]] bool       stageEnabled(std::string_view name) const;
    [[nodiscard]] int        stageCount() const;
    [[nodiscard]] StageDesc& stage(int index);
    [[nodiscard]] StageDesc& stage(std::string_view name);

    /// Emit all stages as RenderCommands into GameWork.
    /// Called by the logic thread during onRender().
    ///
    /// For each enabled stage:
    ///   Clear   → buildClearCommand + buildBindRTCommand
    ///   Geometry → drawList.emitCommands (filtered/sorted)
    ///   Blit    → buildBlitCommand
    ///   Custom  → customFn(work)
    void emit(GameWork& work, const DrawList& drawList,
              const Camera3D& cam, float aspect) const;

    void emit(GameWork& work, const DrawList& drawList,
              const Camera2D& cam, int screenW, int screenH) const;

private:
    struct StageEntry {
        StageDesc desc;
        bool      enabled = true;
    };
    std::vector<StageEntry> m_stages;
};

// =========================================================================
//  Predefined pipelines (return configured Pipeline objects)
//
//  These require pre-created shader/RT handles. The Lifecycle module
//  provides convenience functions that create the handles and the pipeline.
// =========================================================================

Pipeline MakeForwardPipeline(RTHandle screenRT = kNullRT);
Pipeline MakeForwardHDRPipeline(RTHandle hdrRT, RTHandle screenRT,
                                ShaderHandle toneMapShader,
                                ShaderHandle bloomShader = kNullShader);

} // namespace spt3d
