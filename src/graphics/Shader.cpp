#include "Shader.h"
#include <iostream>
#include <cstring>

namespace spt {

Shader::~Shader() {
    destroy();
}

Shader::Shader(Shader&& other) noexcept
    : m_program(other.m_program) {
    other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        destroy();
        m_program = other.m_program;
        other.m_program = 0;
    }
    return *this;
}

void Shader::destroy() {
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

GLuint Shader::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[Shader] Compilation failed: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

bool Shader::linkProgram(GLuint vs, GLuint fs) {
    m_program = glCreateProgram();

    glBindAttribLocation(m_program, 0, "a_position");
    glBindAttribLocation(m_program, 1, "a_color");
    glBindAttribLocation(m_program, 2, "a_texcoord");

    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    GLint success = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_program, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[Shader] Linking failed: " << infoLog << std::endl;
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    return true;
}

bool Shader::loadFromSource(const std::string& vertexSrc, const std::string& fragmentSrc) {
    destroy();

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc.c_str());
    if (vs == 0) return false;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc.c_str());
    if (fs == 0) {
        glDeleteShader(vs);
        return false;
    }

    bool result = linkProgram(vs, fs);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return result;
}

void Shader::use() const {
    if (m_program != 0) {
        glUseProgram(m_program);
    }
}

void Shader::setInt(const char* name, int value) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform1i(loc, value);
}

void Shader::setFloat(const char* name, float value) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform1f(loc, value);
}

void Shader::setVec2(const char* name, float x, float y) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform2f(loc, x, y);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform4f(loc, x, y, z, w);
}

void Shader::setMat4(const char* name, const float* matrix) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
}

}
