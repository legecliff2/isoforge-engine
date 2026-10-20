#include "world/chunk_streamer.hpp"

#include <algorithm>
#include <span>

#include "base/canonical_hash.hpp"
#include "rng/rng.hpp"
#include "world/chunk_codec.hpp"

namespace eng::world {

namespace {
int32_t floor_div(int32_t v) noexcept { return v >= 0 ? v / kChunkSize : -((-v + kChunkSize - 1) / kChunkSize); }
std::string coord_str(ChunkCoord c) {
    return std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.level);
}
} // namespace

ChunkCoord chunk_of_tile(Vec2i t, int16_t level) noexcept { return {floor_div(t.x), floor_div(t.y), level}; }

ChunkStreamer::ChunkStreamer(IChunkProvider& root, uint32_t worker_threads, StreamerConfig cfg)
    : root_(root), cfg_(cfg) {
    for (uint32_t i = 0; i < worker_threads; ++i) threads_.emplace_back([this] { worker(); });
}

ChunkStreamer::~ChunkStreamer() {
    {
        std::lock_guard lk(mu_);
        stop_ = true;
    }
    cv_work_.notify_all();
    for (auto& t : threads_) t.join();
}

void ChunkStreamer::worker() {
    for (;;) {
        ChunkCoord c{};
        {
            std::unique_lock lk(mu_);
            cv_work_.wait(lk, [&] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            c = queue_.front();
            queue_.pop_front();
        }
        ChunkResult r = root_.load(ChunkRequest{c, cfg_.world_seed, cfg_.gen_version});
        {
            std::lock_guard lk(mu_);
            done_.push_back({c, std::move(r)});
        }
        cv_done_.notify_all();
    }
}

void ChunkStreamer::set_focus(Vec2i world_tile, int16_t level, int radius) {
    const Focus f{world_tile, level, radius};
    set_foci(std::span<const Focus>(&f, 1));
}

void ChunkStreamer::set_foci(std::span<const Focus> foci) {
    foci_.assign(foci.begin(), foci.end());
    rebuild_wanted();
}

void ChunkStreamer::pin(uint32_t owner, std::span<const ChunkCoord> chunks) {
    if (chunks.empty()) pins_.erase(owner);
    else pins_[owner].assign(chunks.begin(), chunks.end());
    rebuild_wanted();
}

std::vector<ChunkCoord> ChunkStreamer::pinned() const {
    std::set<ChunkCoord> s;
    for (const auto& kv : pins_) s.insert(kv.second.begin(), kv.second.end());
    return {s.begin(), s.end()};
}

void ChunkStreamer::rebuild_wanted() {
    wanted_.clear();
    wanted_order_.clear();
    auto add = [&](ChunkCoord c) {
        if (!root_.covers(c) || !wanted_.insert(c).second) return;
        wanted_order_.push_back(c);
    };
    // Queue order: pinned halos first (combat needs them), then each focus in ring order
    // (Chebyshev distance, then coord order; nearest first). Foci are visited sorted so the
    // order, like the set, doesn't depend on argument order.
    for (ChunkCoord p : pinned())
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) add({p.x + dx, p.y + dy, p.level});
    std::vector<Focus> fs = foci_;
    std::sort(fs.begin(), fs.end(), [](const Focus& a, const Focus& b) {
        const ChunkCoord ca = chunk_of_tile(a.world_tile, a.level), cb = chunk_of_tile(b.world_tile, b.level);
        if (!(ca == cb)) return ca < cb;
        return a.radius_chunks < b.radius_chunks;
    });
    for (const Focus& fo : fs) {
        const ChunkCoord f = chunk_of_tile(fo.world_tile, fo.level);
        const int radius = std::max(fo.radius_chunks, 0);
        for (int d = 0; d <= radius; ++d)
            for (int dy = -d; dy <= d; ++dy)
                for (int dx = -d; dx <= d; ++dx) {
                    if (std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy) != d) continue;
                    add({f.x + dx, f.y + dy, fo.level});
                }
    }
    if (cfg_.request_shuffle_seed) {
        rng::Pcg32 r(cfg_.request_shuffle_seed, 7);
        r.shuffle(std::span<ChunkCoord>(wanted_order_));
    }
}

void ChunkStreamer::retry(ChunkCoord c) { failed_.erase(c); }
void ChunkStreamer::retry_failed() { failed_.clear(); }

bool ChunkStreamer::known_absent(ChunkCoord c) const {
    if (!root_.covers(c)) return true;
    return empty_.count(c) != 0;   // provider answered Empty (pure in coord: kept even when unwanted)
}

HaloState ChunkStreamer::halo_state(ChunkCoord c) const {
    bool pending = false;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const ChunkCoord n{c.x + dx, c.y + dy, c.level};
            if (resident_.count(n) || known_absent(n)) continue;
            if (failed_.count(n)) return HaloState::Failed;
            pending = true;
        }
    return pending ? HaloState::Pending : HaloState::Complete;
}

void ChunkStreamer::integrate(ChunkCoord c, ChunkResult r) {
    in_flight_.erase(c);
    if (!wanted_.count(c)) {   // focus moved on: keep it in the cache if it loaded
        if (r.status == ChunkStatus::Ready && r.chunk && cfg_.cache_chunks) {
            cache_lru_.remove(c);
            cache_lru_.push_front(c);
            cache_[c] = std::move(*r.chunk);
        }
        return;
    }
    if (r.status == ChunkStatus::Ready && r.chunk) {
        if (r.chunk->coord != c) { failed_[c] = "provider returned chunk " + coord_str(r.chunk->coord); return; }
        resident_[c] = std::move(*r.chunk);
    } else if (r.status == ChunkStatus::Empty) {
        failed_.erase(c);   // nothing there: not resident, not an error
        empty_.insert(c);
    } else {
        failed_[c] = r.error.empty() ? std::string("load failed") : r.error;
    }
}

void ChunkStreamer::enqueue_missing() {
    std::vector<ChunkCoord> to_queue;
    for (ChunkCoord c : wanted_order_) {
        if (resident_.count(c) || failed_.count(c) || empty_.count(c) || in_flight_.count(c)) continue;
        if (auto it = cache_.find(c); it != cache_.end()) {   // cache hit: integrate now
            resident_[c] = std::move(it->second);
            cache_.erase(it);
            cache_lru_.remove(c);
            continue;
        }
        to_queue.push_back(c);
    }
    for (ChunkCoord c : to_queue) {
        in_flight_.insert(c);
        ++loads_started_;
    }
    if (threads_.empty()) {
        for (ChunkCoord c : to_queue) {
            std::lock_guard lk(mu_);
            done_.push_back({c, root_.load(ChunkRequest{c, cfg_.world_seed, cfg_.gen_version})});
        }
        return;
    }
    {
        std::lock_guard lk(mu_);
        for (ChunkCoord c : to_queue) queue_.push_back(c);
    }
    cv_work_.notify_all();
}

std::set<ChunkCoord> ChunkStreamer::resident_keys() const {
    std::set<ChunkCoord> out;
    for (const auto& kv : resident_) out.insert(kv.first);
    return out;
}

StreamDelta ChunkStreamer::diff_from(const std::set<ChunkCoord>& before) const {
    StreamDelta d;   // both walks are in coord order, so both lists come out sorted
    for (const auto& kv : resident_) if (!before.count(kv.first)) d.added.push_back(kv.first);
    for (ChunkCoord c : before) if (!resident_.count(c)) d.removed.push_back(c);
    return d;
}

StreamDelta ChunkStreamer::update() {
    const std::set<ChunkCoord> before = resident_keys();
    // 1. Evict resident chunks that are no longer wanted (to the LRU cache); forget failures.
    for (auto it = resident_.begin(); it != resident_.end();) {
        if (wanted_.count(it->first)) { ++it; continue; }
        if (cfg_.cache_chunks) {
            cache_lru_.remove(it->first);
            cache_lru_.push_front(it->first);
            cache_[it->first] = std::move(it->second);
        }
        it = resident_.erase(it);
    }
    for (auto it = failed_.begin(); it != failed_.end();) it = wanted_.count(it->first) ? std::next(it) : failed_.erase(it);
    {   // Drop queued (not started) loads that are no longer wanted.
        std::lock_guard lk(mu_);
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (wanted_.count(*it)) { ++it; continue; }
            in_flight_.erase(*it);
            it = queue_.erase(it);
        }
    }
    while (cache_.size() > cfg_.cache_chunks && !cache_lru_.empty()) {
        cache_.erase(cache_lru_.back());
        cache_lru_.pop_back();
    }
    // 2. Queue what is missing (or load it inline without workers).
    enqueue_missing();
    // 3. Integrate finished loads in coord order (completion order is irrelevant).
    std::vector<Done> done;
    {
        std::lock_guard lk(mu_);
        done.swap(done_);
    }
    std::sort(done.begin(), done.end(), [](const Done& a, const Done& b) { return a.coord < b.coord; });
    for (auto& d : done) integrate(d.coord, std::move(d.result));
    last_delta_ = diff_from(before);
    return last_delta_;
}

bool ChunkStreamer::idle() const {
    for (ChunkCoord c : wanted_)
        if (!resident_.count(c) && !failed_.count(c) && !empty_.count(c)) return false;
    return true;
}

StreamDelta ChunkStreamer::wait_idle() {
    const std::set<ChunkCoord> before = resident_keys();
    update();
    while (!idle()) {
        {
            std::unique_lock lk(mu_);
            cv_done_.wait(lk, [&] { return !done_.empty(); });
        }
        update();
    }
    last_delta_ = diff_from(before);
    return last_delta_;
}

const Chunk* ChunkStreamer::get(ChunkCoord c) const {
    auto it = resident_.find(c);
    return it == resident_.end() ? nullptr : &it->second;
}

std::vector<const Chunk*> ChunkStreamer::resident(int16_t level) const {
    std::vector<const Chunk*> out;
    for (const auto& [c, ch] : resident_)
        if (c.level == level) out.push_back(&ch);
    return out;
}

std::vector<ChunkCoord> ChunkStreamer::wanted() const { return {wanted_.begin(), wanted_.end()}; }

std::vector<std::string> ChunkStreamer::errors() const {
    std::vector<std::string> out;
    for (const auto& [c, e] : failed_) out.push_back(coord_str(c) + ": " + e);
    return out;
}

uint64_t ChunkStreamer::digest() const {
    CanonicalHasher h;
    h.u32(uint32_t(resident_.size()));
    for (const auto& [c, ch] : resident_) {
        h.i32(c.x);
        h.i32(c.y);
        h.i16(c.level);
        h.byte(uint8_t(ch.origin));
        h.u64(compute_content_hash(ch));
        h.u32(uint32_t(ch.spawns.size()));
    }
    return h.value();
}

} // namespace eng::world
