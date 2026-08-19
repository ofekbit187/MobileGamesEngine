#pragma once

// Entity identity (task 3.2). An EntityId is an index + generation pair:
// indices recycle through a free list (fixed, budget-declared capacity — P1),
// generations make stale ids detectably dead. Systems reference entities and
// assets by id, never by pointer, so storage can move and content can stream
// beneath them (P2).

#include <cstdint>
#include <vector>

namespace mge {

struct EntityId {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool operator==(const EntityId& r) const {
        return index == r.index && generation == r.generation;
    }
    bool operator!=(const EntityId& r) const { return !(*this == r); }
};

constexpr EntityId kInvalidEntity{};

class EntityRegistry {
public:
    explicit EntityRegistry(uint32_t capacity) : capacity_(capacity) {
        generations_.resize(capacity, 0);
        alive_.resize(capacity, false);
        freeList_.reserve(capacity);
        for (uint32_t i = 0; i < capacity; ++i) {
            freeList_.push_back(capacity - 1 - i);  // ascending allocation order
        }
    }

    // Returns kInvalidEntity when the registry is full (refuse, never grow).
    EntityId create() {
        if (freeList_.empty()) return kInvalidEntity;
        const uint32_t index = freeList_.back();
        freeList_.pop_back();
        alive_[index] = true;
        ++liveCount_;
        return {index, generations_[index]};
    }

    void destroy(EntityId id) {
        if (!isAlive(id)) return;
        alive_[id.index] = false;
        ++generations_[id.index];  // stale ids die here
        freeList_.push_back(id.index);
        --liveCount_;
    }

    bool isAlive(EntityId id) const {
        return id.index < capacity_ && alive_[id.index] && generations_[id.index] == id.generation;
    }

    uint32_t capacity() const { return capacity_; }
    uint32_t liveCount() const { return liveCount_; }

private:
    uint32_t capacity_;
    uint32_t liveCount_ = 0;
    std::vector<uint32_t> generations_;
    std::vector<bool> alive_;
    std::vector<uint32_t> freeList_;
};

}  // namespace mge
