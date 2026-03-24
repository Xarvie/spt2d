#pragma once

#include <cstdint>
#include <string>

namespace spt {

enum class ResType : uint8_t {
    Invalid = 0,
    Texture = 1,
    Font    = 2,
    Audio   = 3,
    RawData = 4,
    Atlas   = 5,
    Spine   = 6,
    MaxTypes = 7
};

enum class ResState : uint8_t {
    Unloaded = 0,
    Loading  = 1,
    Loaded   = 2,
    Ready    = 3,
    Failed   = 4
};

enum ResTag : uint8_t {
    ResTag_None      = 0,
    ResTag_Permanent = 1,
    ResTag_Level1    = 10,
    ResTag_Level2    = 11,
    ResTag_Level3    = 12,
    ResTag_UI        = 20,
    ResTag_Custom    = 100
};

const char* ResTypeToString(ResType type);
const char* ResStateToString(ResState state);

struct ResHandle {
    uint32_t value;

    ResHandle() : value(0) {}
    explicit ResHandle(uint32_t v) : value(v) {}

    static ResHandle make(uint16_t index, uint8_t generation, uint8_t tag = 0) {
        return ResHandle(
            (static_cast<uint32_t>(tag) << 24) |
            (static_cast<uint32_t>(generation) << 16) |
            (static_cast<uint32_t>(index))
        );
    }

    static ResHandle Invalid() { return ResHandle(0); }

    uint16_t getIndex() const {
        return static_cast<uint16_t>(value & 0xFFFF);
    }

    uint8_t getGeneration() const {
        return static_cast<uint8_t>((value >> 16) & 0xFF);
    }

    uint8_t getTag() const {
        return static_cast<uint8_t>((value >> 24) & 0xFF);
    }

    bool isValid() const {
        return value != 0;
    }

    bool operator==(const ResHandle& o) const { return value == o.value; }
    bool operator!=(const ResHandle& o) const { return value != o.value; }
    bool operator<(const ResHandle& o) const { return value < o.value; }

    uint32_t toU32() const { return value; }
    static ResHandle fromU32(uint32_t v) { return ResHandle(v); }
};

}
