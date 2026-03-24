#include "../Spt3D.h"
#include "../glad/glad.h"

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>

namespace spt3d {

static LogLvl g_logLevel = LogLvl::Info;
static LogFn g_logCallback = nullptr;

GPUCaps GetCaps() {
    GPUCaps caps;
    
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps.max_tex_size);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &caps.max_tex_units);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &caps.max_color_attachments);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &caps.max_draw_buffers);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &caps.max_vertex_attribs);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &caps.max_uniform_block_size);
    
    caps.float_tex = true;
    caps.float_linear = true;
    caps.color_buf_float = true;
    caps.etc2 = true;
    caps.astc = false;
    
    return caps;
}

void SetLogLevel(LogLvl lvl) {
    g_logLevel = lvl;
}

void Log(LogLvl lvl, const char* fmt, ...) {
    if (static_cast<int>(lvl) < static_cast<int>(g_logLevel)) return;
    
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    if (g_logCallback) {
        g_logCallback(lvl, buffer);
    } else {
        const char* prefix = "";
        switch (lvl) {
            case LogLvl::Trace: prefix = "[TRACE] "; break;
            case LogLvl::Debug: prefix = "[DEBUG] "; break;
            case LogLvl::Info:  prefix = "[INFO] "; break;
            case LogLvl::Warn:  prefix = "[WARN] "; break;
            case LogLvl::Error: prefix = "[ERROR] "; break;
            case LogLvl::Fatal: prefix = "[FATAL] "; break;
            default: break;
        }
        printf("%s%s\n", prefix, buffer);
    }
}

void SetLogCallback(LogFn fn) {
    g_logCallback = std::move(fn);
}

void Clipboard(std::string_view text) {
    (void)text;
}

void GetClipboard(std::function<void(std::string_view, bool)> cb) {
    if (cb) cb("", false);
}

void OpenURL(std::string_view url) {
    (void)url;
}

void Screenshot(std::string_view file) {
    (void)file;
}

}
