#pragma once

#include "../glad/glad.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace spt {

// =============================================================================
//  AttribBinding
//
//  Describes a single vertex-attribute binding that is applied before
//  the program is linked (glBindAttribLocation).  Passing an explicit list
//  makes the binding contract visible in the call site and avoids relying on
//  driver-assigned locations.
// =============================================================================
struct AttribBinding {
    GLuint      location = 0;
    const char* name     = nullptr;
};


// =============================================================================
//  Shader
//
//  RAII wrapper around an OpenGL program object.  Supports vertex + fragment
//  stages, optional geometry stage, pre-link attribute bindings, and a
//  built-in uniform location cache so that repeated setXxx() calls after the
//  first frame cost only a hash-map lookup rather than a GL round-trip.
//
//  Design decisions
//  ─────────────────
//  1. Uniform location cache
//     glGetUniformLocation() can be surprisingly expensive on some drivers
//     (implicit synchronisation, string hashing on the driver side).  The
//     cache is populated lazily on first use and stored inside the Shader
//     object, so it lives exactly as long as the program it describes.
//
//  2. Error log surfaced as std::string
//     Callers can log, display in an in-game console, or assert on the
//     message.  The info log is always retrieved with the full driver-
//     reported length rather than a fixed 512-byte buffer.
//
//  3. Explicit attribute bindings
//     loadFromSource() accepts a span of AttribBinding entries that are
//     applied via glBindAttribLocation before linking.  A default set
//     matching the existing ShaderSources.h convention is provided.
//
//  4. Move-only
//     Copying a program handle would require glCreateProgram + re-link, which
//     is never the right approach.  Copy operations are deleted; move is O(1).
//
//  Thread-safety
//  ──────────────
//  All GL calls must be made on the thread that owns the GL context.
//  The uniform cache is not thread-safe (no locking); it is expected that
//  shaders are created, used, and destroyed on the render thread only.
// =============================================================================
class Shader {
public:
    // Default attribute bindings matching ShaderSources.h conventions.
    static const AttribBinding kDefaultBindings[3];

    Shader()  = default;
    ~Shader();

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // -------------------------------------------------------------------------
    // Loading
    // -------------------------------------------------------------------------

    /// Compile and link a vertex + fragment program.
    ///
    /// \param vertexSrc   GLSL source for the vertex stage.
    /// \param fragmentSrc GLSL source for the fragment stage.
    /// \param bindings    Attribute location bindings applied before linking.
    ///                    Pass nullptr / 0 count to skip (driver assigns).
    /// \returns true on success; false on compile/link failure (see lastError).
    bool loadFromSource(std::string_view      vertexSrc,
                        std::string_view      fragmentSrc,
                        const AttribBinding*  bindings     = kDefaultBindings,
                        std::size_t           bindingCount = 3);

    /// Overload that also accepts an optional geometry shader stage.
    bool loadFromSource(std::string_view      vertexSrc,
                        std::string_view      geometrySrc,
                        std::string_view      fragmentSrc,
                        const AttribBinding*  bindings     = kDefaultBindings,
                        std::size_t           bindingCount = 3);

    /// Release GL resources and reset to the default-constructed state.
    void destroy();

    // -------------------------------------------------------------------------
    // Usage
    // -------------------------------------------------------------------------

    /// glUseProgram(m_program).  No-op if the program is invalid.
    void use() const;

    // -------------------------------------------------------------------------
    // Uniform setters  (all look up location via the cache)
    // -------------------------------------------------------------------------

    void setInt  (std::string_view name, int   value)                             const;
    void setUInt (std::string_view name, unsigned int value)                      const;
    void setFloat(std::string_view name, float value)                             const;
    void setBool (std::string_view name, bool  value)                             const;

    void setVec2 (std::string_view name, float x, float y)                       const;
    void setVec3 (std::string_view name, float x, float y, float z)              const;
    void setVec4 (std::string_view name, float x, float y, float z, float w)     const;

    void setIVec2(std::string_view name, int x, int y)                           const;
    void setIVec3(std::string_view name, int x, int y, int z)                    const;
    void setIVec4(std::string_view name, int x, int y, int z, int w)             const;

    /// Column-major 4×4 matrix.  `transpose` defaults to GL_FALSE (column-major).
    void setMat3 (std::string_view name, const float* matrix,
                  GLboolean transpose = GL_FALSE)                                 const;
    void setMat4 (std::string_view name, const float* matrix,
                  GLboolean transpose = GL_FALSE)                                 const;

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /// Returns the cached uniform location, or -1 if the name is not found.
    /// The result is cached on first call; subsequent calls are O(1).
    [[nodiscard]] GLint uniformLocation(std::string_view name) const;

    /// Returns the attribute location reported by the driver, or -1 if not found.
    [[nodiscard]] GLint attribLocation(std::string_view name) const;

    [[nodiscard]] GLuint program()  const noexcept { return m_program; }
    [[nodiscard]] bool   isValid()  const noexcept { return m_program != 0; }

    /// The most recent compile or link error message, if any.
    [[nodiscard]] const std::string& lastError() const noexcept { return m_lastError; }

    /// Clear the uniform location cache (call after hot-reloading a shader).
    void clearCache() { m_uniformCache.clear(); }

private:
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Compile a single shader stage.  Returns 0 and populates m_lastError on
    /// failure; the caller is responsible for calling glDeleteShader on success.
    [[nodiscard]] GLuint compileStage(GLenum type, std::string_view source);

    /// Link the program from already-compiled stage objects.
    /// Detaches and deletes all stages on both success and failure.
    [[nodiscard]] bool linkStages(const GLuint*  stages,
                                  std::size_t    count,
                                  const AttribBinding* bindings,
                                  std::size_t    bindingCount);

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------
    GLuint      m_program = 0;
    std::string m_lastError;

    // Lazy-populated location cache.  string_view keys are NOT stored; the
    // map uses std::string so lookups with string_view use heterogeneous find
    // (C++14 transparent comparator).
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
        std::size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    struct StringEqual {
        using is_transparent = void;
        bool operator()(const std::string& a, std::string_view b) const noexcept { return a == b; }
        bool operator()(std::string_view  a, const std::string& b) const noexcept { return a == b; }
        bool operator()(const std::string& a, const std::string& b) const noexcept { return a == b; }
    };

    mutable std::unordered_map<std::string, GLint, StringHash, StringEqual>
        m_uniformCache;
};

} // namespace spt
