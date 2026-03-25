#ifdef __WXGAME__

#include "WxApi.h"
#include <emscripten/emscripten.h>
#include <map>
#include <memory>

namespace spt3d {
namespace wxapi {

// =========================================================================
//  用户信息
// =========================================================================

static UserInfoCallback g_userInfoCallback = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnUserInfo(const char* json, int success, const char* error) {
    if (g_userInfoCallback) {
        UserInfo info;
        if (success && json) {
            EM_ASM({
                var obj = JSON.parse(UTF8ToString($0));
                setValue($1 + 0, allocateUTF8(obj.nickName || ""), "*");
                setValue($1 + 4, allocateUTF8(obj.avatarUrl || ""), "*");
                setValue($1 + 8, allocateUTF8(obj.gender || "unknown"), "*");
                setValue($1 + 12, allocateUTF8(obj.city || ""), "*");
                setValue($1 + 16, allocateUTF8(obj.province || ""), "*");
                setValue($1 + 20, allocateUTF8(obj.country || ""), "*");
                setValue($1 + 24, allocateUTF8(obj.language || ""), "*");
            }, json, &info);
        }
        g_userInfoCallback(info, success != 0, error ? error : "");
        g_userInfoCallback = nullptr;
    }
}

void getUserInfo(UserInfoCallback callback) {
    g_userInfoCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.getUserInfo({
                success: function(res) {
                    var userInfo = res.userInfo || {};
                    var u = {
                        nickName: userInfo.nickName || "",
                        avatarUrl: userInfo.avatarUrl || "",
                        gender: userInfo.gender === 1 ? "male" : (userInfo.gender === 2 ? "female" : "unknown"),
                        city: userInfo.city || "",
                        province: userInfo.province || "",
                        country: userInfo.country || "",
                        language: userInfo.language || ""
                    };
                    var json = JSON.stringify(u);
                    var ptr = allocateUTF8(json);
                    Module.ccall("WxApi_OnUserInfo", "void", ["number", "number", "string"], [ptr, 1, ""]);
                    _free(ptr);
                },
                fail: function(err) {
                    Module.ccall("WxApi_OnUserInfo", "void", ["number", "number", "string"], [0, 0, err.errMsg || "getUserInfo failed"]);
                }
            });
        }
    });
}

void getUserProfile(const std::string& desc, UserInfoCallback callback) {
    g_userInfoCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.getUserProfile({
                desc: UTF8ToString($0),
                success: function(res) {
                    var userInfo = res.userInfo || {};
                    var u = JSON.stringify(userInfo);
                    var ptr = allocateUTF8(u);
                    Module.ccall("WxApi_OnUserInfo", "void", ["number", "number", "string"], [ptr, 1, ""]);
                    _free(ptr);
                },
                fail: function(err) {
                    Module.ccall("WxApi_OnUserInfo", "void", ["number", "number", "string"], [0, 0, err.errMsg || "getUserProfile failed"]);
                }
            });
        }
    }, desc.c_str());
}

// =========================================================================
//  登录
// =========================================================================

static LoginCallback g_loginCallback = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnLogin(const char* code, const char* error) {
    if (g_loginCallback) {
        LoginResult result;
        if (code) {
            result.code = code;
            result.success = true;
        }
        if (error) result.errMsg = error;
        g_loginCallback(result);
        g_loginCallback = nullptr;
    }
}

void login(LoginCallback callback) {
    g_loginCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.login({
                success: function(res) {
                    Module.ccall("WxApi_OnLogin", "void", ["string", "string"], [res.code || "", ""]);
                },
                fail: function(err) {
                    Module.ccall("WxApi_OnLogin", "void", ["string", "string"], ["", err.errMsg || "login failed"]);
                }
            });
        }
    });
}

static std::function<void(bool)> g_checkSessionCallback = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnCheckSession(int valid) {
    if (g_checkSessionCallback) {
        g_checkSessionCallback(valid != 0);
        g_checkSessionCallback = nullptr;
    }
}

void checkSession(std::function<void(bool valid)> callback) {
    g_checkSessionCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.checkSession({
                success: function() { Module.ccall("WxApi_OnCheckSession", "void", ["number"], [1]); },
                fail: function() { Module.ccall("WxApi_OnCheckSession", "void", ["number"], [0]); }
            });
        }
    });
}

// =========================================================================
//  文件系统
// =========================================================================

static ReadFileCallback g_readFileCallback = nullptr;
static WriteFileCallback g_writeFileCallback = nullptr;
static std::function<void(bool)> g_deleteFileCallback = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnReadFile(const uint8_t* data, size_t size, const char* error) {
    if (g_readFileCallback) {
        ReadFileResult result;
        if (data && size > 0) {
            result.data.assign(data, data + size);
            result.success = true;
        }
        if (error) result.errMsg = error;
        g_readFileCallback(result);
        g_readFileCallback = nullptr;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnWriteFile(const char* error) {
    if (g_writeFileCallback) {
        WriteFileResult result;
        result.success = (error == nullptr || error[0] == '\0');
        if (error) result.errMsg = error;
        g_writeFileCallback(result);
        g_writeFileCallback = nullptr;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnDeleteFile(int success) {
    if (g_deleteFileCallback) {
        g_deleteFileCallback(success != 0);
        g_deleteFileCallback = nullptr;
    }
}

void readFile(const std::string& path, ReadFileCallback callback) {
    g_readFileCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.getFileSystemManager().readFile({
                filePath: UTF8ToString($0),
                encoding: "binary",
                success: function(res) {
                    var data = new Uint8Array(res.data);
                    var ptr = _malloc(data.length);
                    HEAPU8.set(data, ptr);
                    Module.ccall("WxApi_OnReadFile", "void", ["number", "number", "string"], [ptr, data.length, ""]);
                    _free(ptr);
                },
                fail: function(err) {
                    Module.ccall("WxApi_OnReadFile", "void", ["number", "number", "string"], [0, 0, err.errMsg || "readFile failed"]);
                }
            });
        }
    }, path.c_str());
}

void writeFile(const std::string& path, const uint8_t* data, size_t size, WriteFileCallback callback) {
    g_writeFileCallback = callback;
    std::vector<uint8_t> dataCopy(data, data + size);
    EM_ASM({
        if (typeof wx !== "undefined") {
            var arr = new Uint8Array($2);
            for (var i = 0; i < $2; i++) arr[i] = HEAPU8[$1 + i];
            wx.getFileSystemManager().writeFile({
                filePath: UTF8ToString($0),
                data: arr.buffer,
                success: function() {
                    Module.ccall("WxApi_OnWriteFile", "void", ["string"], [""]);
                },
                fail: function(err) {
                    Module.ccall("WxApi_OnWriteFile", "void", ["string"], [err.errMsg || "writeFile failed"]);
                }
            });
        }
    }, path.c_str(), dataCopy.data(), size);
}

void writeFile(const std::string& path, const std::string& content, WriteFileCallback callback) {
    writeFile(path, reinterpret_cast<const uint8_t*>(content.data()), content.size(), callback);
}

void deleteFile(const std::string& path, std::function<void(bool success)> callback) {
    g_deleteFileCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.getFileSystemManager().unlink({
                filePath: UTF8ToString($0),
                success: function() { Module.ccall("WxApi_OnDeleteFile", "void", ["number"], [1]); },
                fail: function() { Module.ccall("WxApi_OnDeleteFile", "void", ["number"], [0]); }
            });
        }
    }, path.c_str());
}

bool existsFile(const std::string& path) {
    bool exists = false;
    EM_ASM({
        if (typeof wx !== "undefined") {
            try {
                wx.getFileSystemManager().accessSync(UTF8ToString($0));
                setValue($1, 1, "i32");
            } catch(e) {
                setValue($1, 0, "i32");
            }
        }
    }, path.c_str(), &exists);
    return exists;
}

std::vector<std::string> listFiles(const std::string& dirPath) {
    std::vector<std::string> files;
    EM_ASM({
        if (typeof wx !== "undefined") {
            try {
                var list = wx.getFileSystemManager().readdirSync(UTF8ToString($0));
            } catch(e) {}
        }
    }, dirPath.c_str());
    return files;
}

// =========================================================================
//  分享
// =========================================================================

void shareAppMessage(const ShareConfig& config, ShareCallback callback) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.shareAppMessage({
                title: UTF8ToString($0),
                imageUrl: UTF8ToString($1),
                query: UTF8ToString($2),
                success: function() {},
                fail: function(err) {}
            });
        }
    }, config.title.c_str(), config.imageUrl.c_str(), config.query.c_str());
}

void shareAppMessageInteractive(const ShareConfig& config) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.shareAppMessage({
                title: UTF8ToString($0),
                imageUrl: UTF8ToString($1),
                query: UTF8ToString($2)
            });
        }
    }, config.title.c_str(), config.imageUrl.c_str(), config.query.c_str());
}

// =========================================================================
//  提示框
// =========================================================================

void showToast(const ToastConfig& config) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.showToast({
                title: UTF8ToString($0),
                icon: UTF8ToString($1),
                duration: $2,
                mask: $3
            });
        }
    }, config.title.c_str(), config.icon.c_str(), config.duration, config.mask);
}

void hideToast() {
    EM_ASM({ if (typeof wx !== "undefined") wx.hideToast(); });
}

static ModalCallback g_modalCallback = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnModal(int confirm, int cancel) {
    if (g_modalCallback) {
        ModalResult result;
        result.confirm = confirm != 0;
        result.cancel = cancel != 0;
        g_modalCallback(result);
        g_modalCallback = nullptr;
    }
}

void showModal(const ModalConfig& config, ModalCallback callback) {
    g_modalCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.showModal({
                title: UTF8ToString($0),
                content: UTF8ToString($1),
                confirmText: UTF8ToString($2),
                cancelText: UTF8ToString($3),
                showCancel: $4,
                success: function(res) {
                    Module.ccall("WxApi_OnModal", "void", ["number", "number"], [res.confirm ? 1 : 0, res.cancel ? 1 : 0]);
                }
            });
        }
    }, config.title.c_str(), config.content.c_str(), 
       config.confirmText.c_str(), config.cancelText.c_str(), config.showCancel);
}

void showLoading(const std::string& title, bool mask) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.showLoading({ title: UTF8ToString($0), mask: $1 });
        }
    }, title.c_str(), mask);
}

void hideLoading() {
    EM_ASM({ if (typeof wx !== "undefined") wx.hideLoading(); });
}

// =========================================================================
//  剪贴板
// =========================================================================

void setClipboardData(const std::string& data) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.setClipboardData({ data: UTF8ToString($0) });
        }
    }, data.c_str());
}

static std::function<void(const std::string&, bool)> g_clipboardCallback = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void WxApi_OnClipboard(const char* data, int success) {
    if (g_clipboardCallback) {
        g_clipboardCallback(data ? data : "", success != 0);
        g_clipboardCallback = nullptr;
    }
}

void getClipboardData(std::function<void(const std::string&, bool success)> callback) {
    g_clipboardCallback = callback;
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.getClipboardData({
                success: function(res) {
                    var ptr = allocateUTF8(res.data || "");
                    Module.ccall("WxApi_OnClipboard", "void", ["string", "number"], [ptr, 1]);
                    _free(ptr);
                },
                fail: function() {
                    Module.ccall("WxApi_OnClipboard", "void", ["string", "number"], ["", 0]);
                }
            });
        }
    });
}

// =========================================================================
//  屏幕
// =========================================================================

void setKeepScreenOn(bool keep) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.setKeepScreenOn({ keepScreenOn: $0 });
        }
    }, keep);
}

void setScreenBrightness(float value) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.setScreenBrightness({ value: Math.max(0, Math.min(1, $0)) });
        }
    }, value);
}

float getScreenBrightness() {
    float value = 0.5f;
    EM_ASM({
        if (typeof wx !== "undefined") {
            try {
                wx.getScreenBrightness({
                    success: function(res) { setValue($0, res.value || 0.5, "float"); }
                });
            } catch(e) {}
        }
    }, &value);
    return value;
}

// =========================================================================
//  音频
// =========================================================================

class InnerAudioContextImpl : public InnerAudioContext {
public:
    InnerAudioContextImpl() {
        m_id = ++s_nextId;
        s_instances[m_id] = this;
    }
    
    ~InnerAudioContextImpl() override {
        EM_ASM({ if (typeof wx !== "undefined") wx.destroyInnerAudioContext($0); }, m_id);
        s_instances.erase(m_id);
    }

    void setSrc(const std::string& src) override {
        m_src = src;
        EM_ASM({
            if (typeof wx !== "undefined") {
                var ctx = wx.createInnerAudioContext();
                ctx.src = UTF8ToString($1);
                Module.audioContexts = Module.audioContexts || {};
                Module.audioContexts[$0] = ctx;
            }
        }, m_id, src.c_str());
    }
    
    void play() override {
        EM_ASM({
            if (typeof wx !== "undefined" && Module.audioContexts && Module.audioContexts[$0]) {
                Module.audioContexts[$0].play();
            }
        }, m_id);
    }
    
    void pause() override {
        EM_ASM({
            if (typeof wx !== "undefined" && Module.audioContexts && Module.audioContexts[$0]) {
                Module.audioContexts[$0].pause();
            }
        }, m_id);
    }
    
    void stop() override {
        EM_ASM({
            if (typeof wx !== "undefined" && Module.audioContexts && Module.audioContexts[$0]) {
                Module.audioContexts[$0].stop();
            }
        }, m_id);
    }
    
    void seek(double position) override {
        EM_ASM({
            if (typeof wx !== "undefined" && Module.audioContexts && Module.audioContexts[$0]) {
                Module.audioContexts[$0].seek = $1;
            }
        }, m_id, position);
    }
    
    void setLoop(bool loop) override {
        EM_ASM({
            if (typeof wx !== "undefined" && Module.audioContexts && Module.audioContexts[$0]) {
                Module.audioContexts[$0].loop = $1;
            }
        }, m_id, loop);
    }
    
    void setVolume(double volume) override {
        EM_ASM({
            if (typeof wx !== "undefined" && Module.audioContexts && Module.audioContexts[$0]) {
                Module.audioContexts[$0].volume = $1;
            }
        }, m_id, volume);
    }
    
    void setAutoplay(bool autoplay) override {
        EM_ASM({
            if (typeof wx !== "undefined" && Module.audioContexts && Module.audioContexts[$0]) {
                Module.audioContexts[$0].autoplay = $1;
            }
        }, m_id, autoplay);
    }
    
    double getDuration() const override { return m_duration; }
    double getCurrentTime() const override { return m_currentTime; }
    bool isPaused() const override { return m_paused; }
    
    void onPlay(std::function<void()> callback) override { m_onPlay = callback; }
    void onPause(std::function<void()> callback) override { m_onPause = callback; }
    void onStop(std::function<void()> callback) override { m_onStop = callback; }
    void onEnded(std::function<void()> callback) override { m_onEnded = callback; }
    void onError(std::function<void(int, const std::string&)> callback) override { m_onError = callback; }
    void onCanplay(std::function<void()> callback) override { m_onCanplay = callback; }

private:
    int m_id;
    std::string m_src;
    double m_duration = 0.0;
    double m_currentTime = 0.0;
    bool m_paused = true;
    
    std::function<void()> m_onPlay, m_onPause, m_onStop, m_onEnded, m_onCanplay;
    std::function<void(int, const std::string&)> m_onError;
    
    static int s_nextId;
    static std::map<int, InnerAudioContextImpl*> s_instances;
};

int InnerAudioContextImpl::s_nextId = 0;
std::map<int, InnerAudioContextImpl*> InnerAudioContextImpl::s_instances;

InnerAudioContext* InnerAudioContext::create() {
    return new InnerAudioContextImpl();
}

// =========================================================================
//  触摸反馈
// =========================================================================

void performFeedback(FeedbackType type) {
    EM_ASM({
        if (typeof wx !== "undefined") {
            wx.vibrateShort({ type: ["light", "medium", "heavy"][$0] || "light" });
        }
    }, static_cast<int>(type));
}

} // namespace wxapi
} // namespace spt3d

#endif // __WXGAME__
