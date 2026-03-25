#include "../Spt3D.h"

namespace spt3d {

struct Sound::Impl {
    float duration = 0;
};

struct Music::Impl {
    float duration = 0;
    float played = 0;
};

ResState Sound::State() const { return p ? ResState::Ready : ResState::Failed; }
bool Sound::Ready() const { return p != nullptr; }
bool Sound::Valid() const { return p != nullptr; }
float Sound::Duration() const { return p ? p->duration : 0; }
Sound::operator bool() const { return p != nullptr; }

ResState Music::State() const { return p ? ResState::Ready : ResState::Failed; }
bool Music::Ready() const { return p != nullptr; }
bool Music::Valid() const { return p != nullptr; }
float Music::Duration() const { return p ? p->duration : 0; }
float Music::Played() const { return p ? p->played : 0; }
Music::operator bool() const { return p != nullptr; }

Sound LoadSound(std::string_view url, Callback cb) {
    (void)url;
    Sound s;
    s.p = std::make_shared<Sound::Impl>();
    if (cb) cb(true);
    return s;
}

Music LoadMusic(std::string_view url, Callback cb) {
    (void)url;
    Music m;
    m.p = std::make_shared<Music::Impl>();
    if (cb) cb(true);
    return m;
}

void Play(Sound s) { (void)s; }
void Stop(Sound s) { (void)s; }
void Pause(Sound s) { (void)s; }
void Resume(Sound s) { (void)s; }
bool Playing(Sound s) { (void)s; return false; }
void SetVolume(Sound s, float v) { (void)s; (void)v; }
void SetPitch(Sound s, float p) { (void)s; (void)p; }
void SetPan(Sound s, float pan) { (void)s; (void)pan; }

void Play(Music m) { (void)m; }
void Stop(Music m) { (void)m; }
void Pause(Music m) { (void)m; }
void Resume(Music m) { (void)m; }
bool Playing(Music m) { (void)m; return false; }
void SetVolume(Music m, float v) { (void)m; (void)v; }
void SetPitch(Music m, float p) { (void)m; (void)p; }

}
