#include "../Spt3D.h"

#include <vector>
#include <algorithm>

namespace spt3d {

struct DrawList::Impl {
    std::vector<DrawItem> items;
    
    Impl() {
        items.reserve(256);
    }
};

DrawList CreateDrawList() {
    DrawList dl;
    dl.p = std::make_shared<DrawList::Impl>();
    return dl;
}

void Clear(DrawList dl) {
    if (!dl.p) return;
    dl.p->items.clear();
}

void Push(DrawList dl, DrawItem item) {
    if (!dl.p) return;
    dl.p->items.push_back(std::move(item));
}

void Push(DrawList dl, Mesh mesh, Material mat, Mat4 transform) {
    if (!dl.p) return;
    DrawItem item;
    item.mesh = std::move(mesh);
    item.mat = std::move(mat);
    item.transform = transform;
    item.sort_key = 0;
    dl.p->items.push_back(std::move(item));
}

void Push(DrawList dl, Mesh mesh, Material mat, Transform transform) {
    if (!dl.p) return;
    DrawItem item;
    item.mesh = std::move(mesh);
    item.mat = std::move(mat);
    item.transform = ToMat4(transform);
    item.sort_key = 0;
    dl.p->items.push_back(std::move(item));
}

void Push(DrawList dl, Model model, Transform transform) {
    if (!dl.p || !model.Valid()) return;
    
    Mat4 modelMat = ToMat4(transform);
    int meshCount = model.MeshCount();
    
    for (int i = 0; i < meshCount; ++i) {
        Mesh mesh = model.GetMesh(i);
        Material mat = model.GetMat(i);
        
        if (mesh.Valid()) {
            DrawItem item;
            item.mesh = std::move(mesh);
            item.mat = std::move(mat);
            item.transform = modelMat;
            item.sort_key = 0;
            dl.p->items.push_back(std::move(item));
        }
    }
}

void Push(DrawList dl, Model model, Mat4 transform) {
    if (!dl.p || !model.Valid()) return;
    
    int meshCount = model.MeshCount();
    
    for (int i = 0; i < meshCount; ++i) {
        Mesh mesh = model.GetMesh(i);
        Material mat = model.GetMat(i);
        
        if (mesh.Valid()) {
            DrawItem item;
            item.mesh = std::move(mesh);
            item.mat = std::move(mat);
            item.transform = transform;
            item.sort_key = 0;
            dl.p->items.push_back(std::move(item));
        }
    }
}

int Count(DrawList dl) {
    return dl.p ? static_cast<int>(dl.p->items.size()) : 0;
}

namespace detail {

std::vector<DrawItem>& GetDrawItems(DrawList dl) {
    static std::vector<DrawItem> empty;
    return dl.p ? dl.p->items : empty;
}

void SortDrawList(DrawList dl, SortMode mode, const Vec3& camPos) {
    if (!dl.p || dl.p->items.empty()) return;
    
    switch (mode) {
        case SortMode::None:
            break;
            
        case SortMode::FrontToBack: {
            auto& items = dl.p->items;
            std::sort(items.begin(), items.end(), [&camPos](const DrawItem& a, const DrawItem& b) {
                // Extract translation directly from column 3 (cheaper than M4Point with zero)
                Vec3 posA = Vec3(a.transform[3]);
                Vec3 posB = Vec3(b.transform[3]);
                // Use squared distance — avoids sqrt, preserves comparison order
                float dist2A = glm::dot(posA - camPos, posA - camPos);
                float dist2B = glm::dot(posB - camPos, posB - camPos);
                return dist2A < dist2B;
            });
            break;
        }
            
        case SortMode::BackToFront: {
            auto& items = dl.p->items;
            std::sort(items.begin(), items.end(), [&camPos](const DrawItem& a, const DrawItem& b) {
                Vec3 posA = Vec3(a.transform[3]);
                Vec3 posB = Vec3(b.transform[3]);
                float dist2A = glm::dot(posA - camPos, posA - camPos);
                float dist2B = glm::dot(posB - camPos, posB - camPos);
                return dist2A > dist2B;
            });
            break;
        }
    }
}

void FilterByTag(DrawList dl, const std::string& include, const std::string& exclude, std::vector<DrawItem*>& out) {
    out.clear();
    if (!dl.p) return;
    
    for (auto& item : dl.p->items) {
        if (!item.mat.Valid()) continue;
        
        std::string_view tag = item.mat.GetTag();
        
        if (!exclude.empty() && tag == exclude) continue;
        if (!include.empty() && tag != include) continue;
        
        out.push_back(&item);
    }
}

}

}
