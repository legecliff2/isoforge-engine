#pragma once
// LayeredProvider (ARCHITECTURE.md §5.2): composes providers by priority, e.g. a handcrafted
// town (FileChunkProvider) inside procedural wilds (ProceduralProvider). For each coord the
// first layer whose covers() is true serves it; later layers are not consulted, so the result
// is a pure function of (layers, coord, seed). Thread-safe if every layer is.
#include <string_view>
#include <vector>

#include "world/chunk.hpp"

namespace eng::world {

class LayeredProvider final : public IChunkProvider {
public:
    // Highest priority first. Layers are not owned and must outlive this provider.
    explicit LayeredProvider(std::vector<IChunkProvider*> layers) : layers_(std::move(layers)) {}
    std::string_view name() const override { return "layered"; }
    bool covers(ChunkCoord c) const override {
        for (auto* l : layers_) if (l->covers(c)) return true;
        return false;
    }
    ChunkResult load(const ChunkRequest& req) override {
        for (auto* l : layers_) if (l->covers(req.coord)) return l->load(req);
        return {ChunkStatus::NotFound, std::nullopt, "no layer covers chunk"};
    }

private:
    std::vector<IChunkProvider*> layers_;
};

} // namespace eng::world
