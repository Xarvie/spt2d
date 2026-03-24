#include "../Spt3D.h"
#include "../platform/Platform.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>

namespace spt3d {

static Key keyCodeFromString(const std::string& code) {
    std::string c = code;
    std::transform(c.begin(), c.end(), c.begin(), ::tolower);
    
    if (c == "a") return Key::A;
    if (c == "b") return Key::B;
    if (c == "c") return Key::C;
    if (c == "d") return Key::D;
    if (c == "e") return Key::E;
    if (c == "f") return Key::F;
    if (c == "g") return Key::G;
    if (c == "h") return Key::H;
    if (c == "i") return Key::I;
    if (c == "j") return Key::J;
    if (c == "k") return Key::K;
    if (c == "l") return Key::L;
    if (c == "m") return Key::M;
    if (c == "n") return Key::N;
    if (c == "o") return Key::O;
    if (c == "p") return Key::P;
    if (c == "q") return Key::Q;
    if (c == "r") return Key::R;
    if (c == "s") return Key::S;
    if (c == "t") return Key::T;
    if (c == "u") return Key::U;
    if (c == "v") return Key::V;
    if (c == "w") return Key::W;
    if (c == "x") return Key::X;
    if (c == "y") return Key::Y;
    if (c == "z") return Key::Z;
    if (c == "0") return Key::D0;
    if (c == "1") return Key::D1;
    if (c == "2") return Key::D2;
    if (c == "3") return Key::D3;
    if (c == "4") return Key::D4;
    if (c == "5") return Key::D5;
    if (c == "6") return Key::D6;
    if (c == "7") return Key::D7;
    if (c == "8") return Key::D8;
    if (c == "9") return Key::D9;
    if (c == "space") return Key::Space;
    if (c == "return" || c == "enter") return Key::Enter;
    if (c == "escape") return Key::Escape;
    if (c == "tab") return Key::Tab;
    if (c == "backspace") return Key::Backspace;
    if (c == "arrowleft" || c == "left") return Key::Left;
    if (c == "arrowright" || c == "right") return Key::Right;
    if (c == "arrowup" || c == "up") return Key::Up;
    if (c == "arrowdown" || c == "down") return Key::Down;
    if (c.find("shift") != std::string::npos) return Key::LShift;
    if (c.find("control") != std::string::npos || c.find("ctrl") != std::string::npos) return Key::LCtrl;
    if (c.find("alt") != std::string::npos) return Key::LAlt;
    return Key::Unknown;
}

static std::unordered_map<int, bool> g_keyState;
static std::unordered_map<int, bool> g_keyPressed;
static std::unordered_map<int, bool> g_keyReleased;
static std::unordered_map<int, bool> g_btnState;
static std::unordered_map<int, bool> g_btnPressed;
static std::unordered_map<int, bool> g_btnReleased;
static Vec2 g_mousePos = {0, 0};
static float g_wheel = 0;
static bool g_cursorVisible = true;

static std::vector<std::pair<int, Vec2>> g_touches;
static std::unordered_map<int, bool> g_pointerState;
static std::unordered_map<int, bool> g_pointerPressed;
static std::unordered_map<int, bool> g_pointerReleased;
static std::unordered_map<int, Vec2> g_pointerPos;

static uint32_t g_gestureFlags = 0;
static Gesture g_detectedGesture = Gesture::None;
static float g_gestureHoldDuration = 0;
static Vec2 g_gestureDragVec = {0, 0};

void InitInput() {
    auto* platform = GetPlatform();
    if (!platform) return;
    
    auto* input = platform->getInputSystem();
    if (!input) return;
    
    input->onKey.connect([](const KeyEvent& e) {
        int key = static_cast<int>(keyCodeFromString(e.code));
        if (e.type == KeyEvent::Down) {
            if (!g_keyState[key]) {
                g_keyPressed[key] = true;
            }
            g_keyState[key] = true;
        } else {
            if (g_keyState[key]) {
                g_keyReleased[key] = true;
            }
            g_keyState[key] = false;
        }
    });
    
    input->onMouse.connect([](const MouseEvent& e) {
        g_mousePos = {e.x, e.y};
        if (e.type == MouseEvent::Wheel) {
            g_wheel = e.deltaY;
        } else {
            int btn = e.button;
            if (e.type == MouseEvent::Down) {
                if (!g_btnState[btn]) {
                    g_btnPressed[btn] = true;
                }
                g_btnState[btn] = true;
            } else if (e.type == MouseEvent::Up) {
                if (g_btnState[btn]) {
                    g_btnReleased[btn] = true;
                }
                g_btnState[btn] = false;
            }
        }
    });
    
    input->onTouch.connect([](const TouchEvent& e) {
        int id = e.id;
        g_pointerPos[id] = {e.x, e.y};
        
        if (e.type == TouchEvent::Begin) {
            g_pointerState[id] = true;
            g_pointerPressed[id] = true;
            g_touches.push_back({id, {e.x, e.y}});
        } else if (e.type == TouchEvent::End) {
            g_pointerState[id] = false;
            g_pointerReleased[id] = true;
            g_touches.erase(
                std::remove_if(g_touches.begin(), g_touches.end(),
                    [id](const auto& p) { return p.first == id; }),
                g_touches.end()
            );
        } else if (e.type == TouchEvent::Move) {
            for (auto& t : g_touches) {
                if (t.first == id) {
                    t.second = {e.x, e.y};
                    break;
                }
            }
        }
    });
}

void ClearInputState() {
    g_keyPressed.clear();
    g_keyReleased.clear();
    g_btnPressed.clear();
    g_btnReleased.clear();
    g_wheel = 0;
    g_pointerPressed.clear();
    g_pointerReleased.clear();
}

bool KeyDown(Key k) {
    auto it = g_keyState.find(static_cast<int>(k));
    return it != g_keyState.end() && it->second;
}

bool KeyPressed(Key k) {
    auto it = g_keyPressed.find(static_cast<int>(k));
    return it != g_keyPressed.end() && it->second;
}

bool KeyReleased(Key k) {
    auto it = g_keyReleased.find(static_cast<int>(k));
    return it != g_keyReleased.end() && it->second;
}

bool BtnDown(Btn b) {
    auto it = g_btnState.find(static_cast<int>(b));
    return it != g_btnState.end() && it->second;
}

bool BtnPressed(Btn b) {
    auto it = g_btnPressed.find(static_cast<int>(b));
    return it != g_btnPressed.end() && it->second;
}

Vec2 MousePos() {
    return g_mousePos;
}

float Wheel() {
    return g_wheel;
}

void SetMouse(int x, int y) {
    g_mousePos = {static_cast<float>(x), static_cast<float>(y)};
}

void ShowCursor() {
    g_cursorVisible = true;
}

void HideCursor() {
    g_cursorVisible = false;
}

int TouchCount() {
    return static_cast<int>(g_touches.size());
}

Vec2 TouchPos(int i) {
    if (i >= 0 && i < static_cast<int>(g_touches.size())) {
        return g_touches[i].second;
    }
    return {0, 0};
}

Vec2 PointerPos(int id) {
    auto it = g_pointerPos.find(id);
    return it != g_pointerPos.end() ? it->second : Vec2{0, 0};
}

bool PointerDown(int id) {
    auto it = g_pointerState.find(id);
    return it != g_pointerState.end() && it->second;
}

bool PointerPressed(int id) {
    auto it = g_pointerPressed.find(id);
    return it != g_pointerPressed.end() && it->second;
}

bool PointerReleased(int id) {
    auto it = g_pointerReleased.find(id);
    return it != g_pointerReleased.end() && it->second;
}

void SetGestures(uint32_t flags) {
    g_gestureFlags = flags;
}

Gesture DetectedGesture() {
    return g_detectedGesture;
}

float GestureHoldDuration() {
    return g_gestureHoldDuration;
}

Vec2 GestureDragVec() {
    return g_gestureDragVec;
}

}
