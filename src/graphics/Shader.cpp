#include "Shader.h"

#include <iostream>
#include <cassert>
#include <array>

namespace spt3d {

// =============================================================================
//  Default attribute bindings  (matches ShaderSources.h layout)
// =============================================================================
const AttribBinding GLShader::kDefaultBindings[3] = {
    { 0, "a_position" },
    { 1, "a_color"    },
    { 2, "a_texcoord" },
};

// =============================================================================
//  Lifecycle
// =============================================================================

GLShader::~GLShader() {
    destroy();
}

GLShader::GLShader(GLShader&& other) noexcept
    : m_program(other.m_program)
    , m_lastError(std::move(other.m_lastError))
    , m_uniformCache(std::move(other.m_uniformCache))
{
    other.m_program = 0;
}

GLShader& GLShader::operator=(GLShader&& other) noexcept {
    if (this != &other) {
        destroy();
        m_program      = other.m_program;
        m_lastError    = std::move(other.m_lastError);
        m_uniformCache = std::move(other.m_uniformCache);
        other.m_program = 0;
    }
    return *this;
}

void GLShader::destroy() {
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_uniformCache.clear();
    m_lastError.clear();
}

// =============================================================================
//  Loading
// =============================================================================

bool GLShader::loadFromSource(std::string_view     vertexSrc,
                             std::string_view     fragmentSrc,
                             const AttribBinding* bindings,
                             std::size_t          bindingCount) {
    destroy();

    const GLuint vs = compileStage(GL_VERTEX_SHADER,   vertexSrc);
    if (vs == 0) return false;

    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    if (fs == 0) {
        glDeleteShader(vs);
        return false;
    }

    const GLuint stages[2] = { vs, fs };
    return linkStages(stages, 2, bindings, bindingCount);
}

bool GLShader::loadFromSource(std::string_view     vertexSrc,
                             std::string_view     geometrySrc,
                             std::string_view     fragmentSrc,
                             const AttribBinding* bindings,
                             std::size_t          bindingCount) {
    destroy();

    const GLuint vs = compileStage(GL_VERTEX_SHADER,   vertexSrc);
    if (vs == 0) return false;

#if defined(GL_GEOMETRY_SHADER)
    const GLuint gs = compileStage(GL_GEOMETRY_SHADER, geometrySrc);
    if (gs == 0) {
        glDeleteShader(vs);
        return false;
    }
#else
    (void)geometrySrc;
    m_lastError = "[Shader] Geometry shaders not supported on this platform";
    std::cerr << m_lastError << '\n';
    glDeleteShader(vs);
    return false;
#endif

    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    if (fs == 0) {
        glDeleteShader(vs);
#if defined(GL_GEOMETRY_SHADER)
        glDeleteShader(gs);
#endif
        return false;
    }

#if defined(GL_GEOMETRY_SHADER)
    const GLuint stages[3] = { vs, gs, fs };
    return linkStages(stages, 3, bindings, bindingCount);
#else
    const GLuint stages[2] = { vs, fs };
    return linkStages(stages, 2, bindings, bindingCount);
#endif
}

// =============================================================================
//  Internal helpers
// =============================================================================

GLuint GLShader::compileStage(GLenum type, std::string_view source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        m_lastError = "[Shader] glCreateShader() returned 0";
        std::cerr << m_lastError << '\n';
        return 0;
    }

    const char*  src    = source.data();
    const GLint  length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src, &length);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);

        std::string log(static_cast<std::size_t>(logLen > 0 ? logLen - 1 : 0), '\0');
        if (logLen > 1) {
            glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        }

        const char* typeName =
            (type == GL_VERTEX_SHADER)   ? "vertex"   :
            (type == GL_FRAGMENT_SHADER) ? "fragment" :
#if defined(GL_GEOMETRY_SHADER)
            (type == GL_GEOMETRY_SHADER) ? "geometry" :
#endif
            "unknown";

        m_lastError  = "[Shader] ";
        m_lastError += typeName;
        m_lastError += " compile error:\n";
        m_lastError += log;

        std::cerr << m_lastError;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

bool GLShader::linkStages(const GLuint*        stages,
                         std::size_t          count,
                         const AttribBinding* bindings,
                         std::size_t          bindingCount) {
    m_program = glCreateProgram();
    if (m_program == 0) {
        m_lastError = "[Shader] glCreateProgram() returned 0";
        std::cerr << m_lastError << '\n';
        for (std::size_t i = 0; i < count; ++i) glDeleteShader(stages[i]);
        return false;
    }

    // Apply explicit attribute bindings before linking so the layout is
    // deterministic regardless of driver/platform.
    for (std::size_t i = 0; i < bindingCount; ++i) {
        if (bindings[i].name && bindings[i].name[0] != '\0') {
            glBindAttribLocation(m_program, bindings[i].location, bindings[i].name);
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        glAttachShader(m_program, stages[i]);
    }

    glLinkProgram(m_program);

    // Detach and delete stage objects regardless of link outcome:
    // the program retains the compiled binary; we no longer need the stage
    // objects, and holding them wastes driver memory.
    for (std::size_t i = 0; i < count; ++i) {
        glDetachShader(m_program, stages[i]);
        glDeleteShader(stages[i]);
    }

    GLint status = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLen = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLen);

        std::string log(static_cast<std::size_t>(logLen > 0 ? logLen - 1 : 0), '\0');
        if (logLen > 1) {
            glGetProgramInfoLog(m_program, logLen, nullptr, log.data());
        }

        m_lastError  = "[Shader] link error:\n";
        m_lastError += log;

        std::cerr << m_lastError;
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    return true;
}

// =============================================================================
//  Usage
// =============================================================================

void GLShader::use() const {
    assert(m_program != 0 && "GLShader::use() called on an invalid program");
    glUseProgram(m_program);
}

// =============================================================================
//  Uniform location cache
// =============================================================================

GLint GLShader::uniformLocation(std::string_view name) const {
    assert(m_program != 0 && "uniformLocation() called on an invalid program");

    std::string key(name);
    auto it = m_uniformCache.find(key);
    if (it != m_uniformCache.end()) return it->second;

    const GLint loc = glGetUniformLocation(m_program, key.c_str());

    m_uniformCache.emplace(std::move(key), loc);

#ifndef NDEBUG
    if (loc == -1) {
        std::cerr << "[Shader] Warning: uniform '" << name
                  << "' not found (inactive or optimised out)\n";
    }
#endif

    return loc;
}

GLint GLShader::attribLocation(std::string_view name) const {
    assert(m_program != 0 && "attribLocation() called on an invalid program");
    return glGetAttribLocation(m_program, std::string(name).c_str());
}

// =============================================================================
//  Uniform setters
// =============================================================================

void GLShader::setInt(std::string_view name, int value) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform1i(loc, value);
}

void GLShader::setUInt(std::string_view name, unsigned int value) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform1ui(loc, value);
}

void GLShader::setFloat(std::string_view name, float value) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform1f(loc, value);
}

void GLShader::setBool(std::string_view name, bool value) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform1i(loc, value ? 1 : 0);
}

void GLShader::setVec2(std::string_view name, float x, float y) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform2f(loc, x, y);
}

void GLShader::setVec3(std::string_view name, float x, float y, float z) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform3f(loc, x, y, z);
}

void GLShader::setVec4(std::string_view name, float x, float y, float z, float w) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform4f(loc, x, y, z, w);
}

void GLShader::setIVec2(std::string_view name, int x, int y) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform2i(loc, x, y);
}

void GLShader::setIVec3(std::string_view name, int x, int y, int z) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform3i(loc, x, y, z);
}

void GLShader::setIVec4(std::string_view name, int x, int y, int z, int w) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniform4i(loc, x, y, z, w);
}

void GLShader::setMat3(std::string_view name, const float* matrix,
                      GLboolean transpose) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniformMatrix3fv(loc, 1, transpose, matrix);
}

void GLShader::setMat4(std::string_view name, const float* matrix,
                      GLboolean transpose) const {
    const GLint loc = uniformLocation(name);
    if (loc != -1) glUniformMatrix4fv(loc, 1, transpose, matrix);
}

} // namespace spt3d
