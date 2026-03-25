// spt3d/platform/WxApi.h — WeChat Mini Game specific APIs
// [THREAD: main] — All WeChat APIs must be called from main thread
#pragma once

#ifdef __WXGAME__

#include <string>
#include <functional>
#include <vector>

namespace spt3d {
namespace wxapi {

// =========================================================================
//  用户信息
// =========================================================================

struct UserInfo {
    std::string nickName;
    std::string avatarUrl;
    std::string gender;      // "male", "female", "unknown"
    std::string city;
    std::string province;
    std::string country;
    std::string language;
};

using UserInfoCallback = std::function<void(const UserInfo&, bool success, const std::string& error)>;

void getUserInfo(UserInfoCallback callback);
void getUserProfile(const std::string& desc, UserInfoCallback callback);

// =========================================================================
//  登录
// =========================================================================

struct LoginResult {
    std::string code;
    std::string errMsg;
    bool success = false;
};

using LoginCallback = std::function<void(const LoginResult&)>;

void login(LoginCallback callback);
void checkSession(std::function<void(bool valid)> callback);

// =========================================================================
//  文件系统
// =========================================================================

struct FileInfo {
    std::string path;
    size_t size = 0;
    int64_t createTime = 0;
    int64_t modifyTime = 0;
};

struct ReadFileResult {
    std::vector<uint8_t> data;
    std::string errMsg;
    bool success = false;
};

struct WriteFileResult {
    std::string errMsg;
    bool success = false;
};

using ReadFileCallback = std::function<void(const ReadFileResult&)>;
using WriteFileCallback = std::function<void(const WriteFileResult&)>;
using FileInfoCallback = std::function<void(const FileInfo&, bool success, const std::string& error)>;

void readFile(const std::string& path, ReadFileCallback callback);
void writeFile(const std::string& path, const uint8_t* data, size_t size, WriteFileCallback callback);
void writeFile(const std::string& path, const std::string& content, WriteFileCallback callback);
void deleteFile(const std::string& path, std::function<void(bool success)> callback);
bool existsFile(const std::string& path);
std::vector<std::string> listFiles(const std::string& dirPath);

// =========================================================================
//  分享
// =========================================================================

struct ShareConfig {
    std::string title;
    std::string imageUrl;
    std::string query;       // URL query params
    std::string imageUrlId;  // for custom images
};

using ShareCallback = std::function<void(bool success, const std::string& errMsg)>;

void shareAppMessage(const ShareConfig& config, ShareCallback callback = nullptr);
void shareAppMessageInteractive(const ShareConfig& config);

// =========================================================================
//  支付
// =========================================================================

struct PaymentConfig {
    std::string timeStamp;
    std::string nonceStr;
    std::string package;
    std::string signType;
    std::string paySign;
};

using PaymentCallback = std::function<void(bool success, const std::string& errMsg)>;

void requestPayment(const PaymentConfig& config, PaymentCallback callback);

// =========================================================================
//  激励视频广告
// =========================================================================

class RewardedVideoAd {
public:
    using LoadCallback = std::function<void(bool success)>;
    using ShowCallback = std::function<void(bool success, bool rewarded)>;
    using CloseCallback = std::function<void(bool rewarded)>;
    using ErrorCallback = std::function<void(int errCode, const std::string& errMsg)>;

    static RewardedVideoAd* create(const std::string& adUnitId);
    virtual ~RewardedVideoAd() = default;

    virtual void load(LoadCallback callback = nullptr) = 0;
    virtual void show(ShowCallback callback = nullptr) = 0;
    virtual void destroy() = 0;
    
    virtual void onClose(CloseCallback callback) = 0;
    virtual void onError(ErrorCallback callback) = 0;
};

// =========================================================================
//  Banner广告
// =========================================================================

class BannerAd {
public:
    static BannerAd* create(const std::string& adUnitId, int styleLeft, int styleTop, int styleWidth);
    virtual ~BannerAd() = default;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void destroy() = 0;
    virtual void resize(int width, int height) = 0;
};

// =========================================================================
//  提示框
// =========================================================================

struct ToastConfig {
    std::string title;
    std::string icon = "none";  // "success", "loading", "none"
    int duration = 1500;
    bool mask = false;
};

struct ModalConfig {
    std::string title;
    std::string content;
    std::string confirmText = "确定";
    std::string cancelText = "取消";
    bool showCancel = true;
};

struct ModalResult {
    bool confirm = false;
    bool cancel = false;
};

using ModalCallback = std::function<void(const ModalResult&)>;

void showToast(const ToastConfig& config);
void hideToast();
void showModal(const ModalConfig& config, ModalCallback callback);
void showLoading(const std::string& title = "加载中", bool mask = true);
void hideLoading();

// =========================================================================
//  导航
// =========================================================================

struct NavigationConfig {
    std::string url;
    std::string path;
    std::string query;
    std::string extraData;  // JSON string
    std::string envVersion = "release";  // "develop", "trial", "release"
};

void navigateToMiniProgram(const NavigationConfig& config, std::function<void(bool success)> callback = nullptr);
void navigateBackMiniProgram(const std::string& extraData = "");

// =========================================================================
//  开放数据域
// =========================================================================

void postMessageToOpenDataContext(const std::string& message);
void setOpenDataContextSharedCanvasSize(int width, int height);

// =========================================================================
//  性能和系统
// =========================================================================

struct PerformanceInfo {
    double fps = 0.0;
    double cpuUsage = 0.0;
    double memoryUsage = 0.0;
};

PerformanceInfo getPerformanceInfo();
void triggerGC();

// =========================================================================
//  音频
// =========================================================================

class InnerAudioContext {
public:
    static InnerAudioContext* create();
    virtual ~InnerAudioContext() = default;

    virtual void setSrc(const std::string& src) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(double position) = 0;
    virtual void setLoop(bool loop) = 0;
    virtual void setVolume(double volume) = 0;
    virtual void setAutoplay(bool autoplay) = 0;
    
    virtual double getDuration() const = 0;
    virtual double getCurrentTime() const = 0;
    virtual bool isPaused() const = 0;
    
    virtual void onPlay(std::function<void()> callback) = 0;
    virtual void onPause(std::function<void()> callback) = 0;
    virtual void onStop(std::function<void()> callback) = 0;
    virtual void onEnded(std::function<void()> callback) = 0;
    virtual void onError(std::function<void(int, const std::string&)> callback) = 0;
    virtual void onCanplay(std::function<void()> callback) = 0;
};

// =========================================================================
//  录屏
// =========================================================================

class GameRecorder {
public:
    static GameRecorder* getInstance();
    virtual ~GameRecorder() = default;

    virtual void start(int duration = 300) = 0;  // max duration in seconds
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual void share() = 0;
    
    virtual bool isRecording() const = 0;
};

// =========================================================================
//  触摸反馈
// =========================================================================

enum class FeedbackType {
    Light = 0,
    Medium = 1,
    Heavy = 2
};

void performFeedback(FeedbackType type = FeedbackType::Light);

// =========================================================================
//  剪贴板
// =========================================================================

void setClipboardData(const std::string& data);
void getClipboardData(std::function<void(const std::string&, bool success)> callback);

// =========================================================================
//  屏幕
// =========================================================================

void setKeepScreenOn(bool keep);
void setScreenBrightness(float value);  // 0.0 - 1.0
float getScreenBrightness();

// =========================================================================
//  键盘
// =========================================================================

struct KeyboardOptions {
    std::string defaultValue;
    int maxLength = 100;
    bool multiple = false;
    std::string confirmType = "done";  // "send", "search", "next", "go", "done"
    bool adjustResize = true;
};

using KeyboardCallback = std::function<void(const std::string& value, bool confirm)>;

void showKeyboard(const KeyboardOptions& options, KeyboardCallback callback);
void hideKeyboard();
void updateKeyboardValue(const std::string& value);

} // namespace wxapi
} // namespace spt3d

#endif // __WXGAME__
