#ifndef RIOT_KEEPER_H
#define RIOT_KEEPER_H

#include "riot.h"


namespace riot::async {

template <typename DataStruct, uint8_t RCU_SET_SIZE = 4>
class keeper {

    struct rcu_policy;

    struct read_policy;

    struct update_policy;

    template <typename Policy> struct rcu_token;

    std::array<DataStruct, RCU_SET_SIZE> _versions;

    std::atomic<uint8_t> _version_index{0};

    std::array<std::atomic<size_t>, RCU_SET_SIZE> _version_stats {};

    lock_atomic_ordered _change_access;

public:

    typedef rcu_token<read_policy> read_token_t;

    typedef rcu_token<update_policy> update_token_t;

    read_token_t read_token() { return read_token_t{this}; }

    update_token_t update_token() { return update_token_t{this}; }
};

} // end namespace riot::async


// -==============================- DEFINITIONS -==================================-


RIOT_AWAITABLE_KEEPER_META
struct RIOT_AWAITABLE_KEEPER_SPACE rcu_policy {

    uint8_t _version {0};

    keeper<DataStruct, RCU_SET_SIZE>* _keeper {nullptr};

    DataStruct* _copy {nullptr};
};

RIOT_AWAITABLE_KEEPER_META
struct RIOT_AWAITABLE_KEEPER_SPACE read_policy : public RIOT_AWAITABLE_KEEPER_SPACE rcu_policy {

    const DataStruct* capture();

    void sync();
};

RIOT_AWAITABLE_KEEPER_META
struct RIOT_AWAITABLE_KEEPER_SPACE update_policy : public RIOT_AWAITABLE_KEEPER_SPACE rcu_policy {

    context<DataStruct*> capture();

    void sync();
};


RIOT_AWAITABLE_KEEPER_META template <typename Policy>
struct RIOT_AWAITABLE_KEEPER_SPACE rcu_token : public Policy {

    rcu_token(keeper<DataStruct, RCU_SET_SIZE>* k) { this->_keeper = k; };

    ~rcu_token() { Policy::sync(); };
};


RIOT_AWAITABLE_KEEPER_META
auto RIOT_AWAITABLE_KEEPER_SPACE read_policy::capture() -> const DataStruct* {
    while (true) {
        auto version = this->_keeper->_version_index.load(std::memory_order_acquire);
        this->_copy = &this->_keeper->_versions[this->_version = version];
        ++this->_keeper->_version_stats[version];
        if (version == this->_keeper->_version_index.load(std::memory_order_acquire)) break;
        else --this->_keeper->_version_stats[version];
    }
    // std::cout << "keeper addr: " << this->_keeper << '\t' << "Captured for Read. Current version readers count: " << this->_keeper->_version_stats[this->_version] << '\n';
    const auto* secure_copy_ptr = this->_copy; 
    return secure_copy_ptr;
}


RIOT_AWAITABLE_KEEPER_META
void RIOT_AWAITABLE_KEEPER_SPACE read_policy::sync() {
    this->_copy = nullptr;
    --this->_keeper->_version_stats[this->_version];
    // std::cout << "keeper addr: " << this->_keeper << '\t' << "Synced after Read. Current version readers count: " << this->_keeper->_version_stats[this->_version] << '\n';
}


RIOT_AWAITABLE_KEEPER_META
auto RIOT_AWAITABLE_KEEPER_SPACE update_policy::capture() -> context<DataStruct*> {
    // std::cout << "keeper addr: " << this->_keeper << '\t' << "Capturing for Update" << '\n';
    co_await this->_keeper->_change_access;

    const auto cur_idx { this->_keeper->_version_index.load() };
    this->_version = (cur_idx + 1) % RCU_SET_SIZE;
    while (this->_keeper->_version_stats[this->_version] > 0) {
        (this->_version += 1) %= RCU_SET_SIZE;
        // std::cout << "Checking for version: " << this->_version << '\n';
        co_suspend;
    }

    this->_copy = &(this->_keeper->_versions[this->_version]
                    = this->_keeper->_versions[cur_idx]);
    // std::cout << "keeper addr: " << this->_keeper << '\t' << "Captured for Update" << '\n';
    co_return this->_copy;
}


RIOT_AWAITABLE_KEEPER_META
void RIOT_AWAITABLE_KEEPER_SPACE update_policy::sync() {
    this->_copy = nullptr;
    this->_keeper->_version_index.store(this->_version);
    this->_keeper->_change_access.sync();
    // std::cout << "keeper addr: " << this->_keeper << '\t' << "Synced after Update" << '\n';
}

#undef RIOT_AWAITABLE_KEEPER_META
#undef RIOT_AWAITABLE_KEEPER_SPACE
#endif // RIOT_KEEPER_H
