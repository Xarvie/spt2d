#pragma once

#include "../glad/glad.h"
#include <string>

namespace spt {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    bool loadFromSource(const std::string& vertexSrc, const std::string& fragmentSrc);
    void use() const;

    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setMat4(const char* name, const float* matrix) const;

    GLuint getProgram() const { return m_program; }
    bool isValid() const { return m_program != 0; }

    void destroy();

private:
    GLuint compileShader(GLenum type, const char* source);
    bool linkProgram(GLuint vs, GLuint fs);

    GLuint m_program = 0;
};

}
