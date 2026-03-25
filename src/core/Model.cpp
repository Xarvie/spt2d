#include "../Spt3D.h"

#include <vector>
#include <limits>

namespace spt3d {

struct Model::Impl {
    struct Part {
        Mesh mesh;
        std::shared_ptr<Material::Impl> material;
    };
    std::vector<Part> parts;
    bool valid = false;
};

ResState Model::State() const { return Valid() ? ResState::Ready : ResState::Failed; }
bool Model::Ready() const { return Valid(); }
bool Model::Valid() const { return p && p->valid; }
int Model::MeshCount() const { return p ? static_cast<int>(p->parts.size()) : 0; }
int Model::MatCount() const { return MeshCount(); }
Model::operator bool() const { return Valid(); }

Mesh Model::GetMesh(int i) const {
    if (!p || i < 0 || i >= static_cast<int>(p->parts.size())) {
        return Mesh();
    }
    return p->parts[i].mesh;
}

Material Model::GetMat(int i) const {
    Material mat;
    if (p && i >= 0 && i < static_cast<int>(p->parts.size())) {
        mat.p = p->parts[i].material;
    }
    return mat;
}

void Model::SetMat(int mesh_idx, Material mat) {
    if (p && mesh_idx >= 0 && mesh_idx < static_cast<int>(p->parts.size())) {
        p->parts[mesh_idx].material = mat.p;
    }
}

AABB Model::Bounds() const {
    if (!p || p->parts.empty()) return AABB{{0,0,0},{0,0,0}};
    
    AABB bounds = { Vec3(std::numeric_limits<float>::max()),
                    Vec3(std::numeric_limits<float>::lowest()) };
    bool any = false;
    for (const auto& part : p->parts) {
        if (part.mesh.Valid()) {
            AABB mb = part.mesh.Bounds();
            if (!any) {
                bounds = mb;
                any = true;
            } else {
                bounds = AABBMerge(bounds, mb);
            }
        }
    }
    return any ? bounds : AABB{{0,0,0},{0,0,0}};
}

Model LoadModel(std::string_view url, Callback cb) {
    Model model;
    model.p = std::make_shared<Model::Impl>();
    
    FsRead(url, [model, cb](const uint8_t* data, size_t size, bool success) {
        if (success && data) {
            model.p->valid = true;
            if (cb) cb(true);
        } else {
            if (cb) cb(false);
        }
    });
    
    return model;
}

Model MakeModel(Mesh mesh, Material mat) {
    Model model;
    model.p = std::make_shared<Model::Impl>();
    Model::Impl::Part part;
    part.mesh = std::move(mesh);
    part.material = mat.p;
    model.p->parts.push_back(std::move(part));
    model.p->valid = true;
    return model;
}

Model MakeModel(std::initializer_list<std::pair<Mesh, Material>> parts) {
    Model model;
    model.p = std::make_shared<Model::Impl>();
    for (auto& [mesh, mat] : parts) {
        Model::Impl::Part part;
        part.mesh = std::move(mesh);
        part.material = mat.p;
        model.p->parts.push_back(std::move(part));
    }
    model.p->valid = true;
    return model;
}

struct Animation::Impl {
    std::string name;
    float duration = 0.0f;
    bool valid = false;
};

bool Animation::Valid() const { return p && p->valid; }
float Animation::Duration() const { return p ? p->duration : 0.0f; }
std::string_view Animation::Name() const { return p ? p->name : ""; }

std::vector<Animation> LoadAnims(std::string_view url, Callback cb) {
    std::vector<Animation> result;
    
    FsRead(url, [cb](const uint8_t* data, size_t size, bool success) {
        if (cb) cb(success);
    });
    
    return result;
}

void Animate(Model model, Animation anim, float time) {
    (void)model;
    (void)anim;
    (void)time;
}

void AnimateBlend(Model model, Animation a, Animation b, float weight) {
    (void)model;
    (void)a;
    (void)b;
    (void)weight;
}

}
