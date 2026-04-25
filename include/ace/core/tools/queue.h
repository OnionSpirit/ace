/**
 * @file
 * @details This file contains a @b queue classes, that provides atomic queues with eject operations
 */
#ifndef ACE_COMMON_QUEUE_H
#define ACE_COMMON_QUEUE_H

#include <vector>
#include <utility>

namespace ace::core::tools {

    template <typename T>
    class queue;

    template<typename T>
    struct q_node {
        q_node* prev = nullptr;
        q_node* next = nullptr;
        queue<T>* owning_queue = nullptr;

        alignas(T) unsigned char storage[sizeof(T)]{};

        T* data() noexcept { return reinterpret_cast<T*>(storage); }
        [[nodiscard]] const T* data() const noexcept { return reinterpret_cast<const T*>(storage); }

        void construct(const T& val) noexcept { new (storage) T(val); }
        void construct(T&& val) noexcept { new (storage) T(std::move(val)); }
        void destruct() { data()->~T(); }

        bool remove() noexcept ;
    };

    template<typename T>
    class slab_mempool {
        q_node<T>* free_head = nullptr;
        q_node<T>* free_tail = nullptr;
        std::vector<q_node<T>*> slabs;
        static constexpr size_t CHUNK_SIZE = 1024;

        void grow() {
            try {
                auto* slab = new q_node<T>[CHUNK_SIZE];
                slabs.push_back(slab);

                for (size_t i = 0; i < CHUNK_SIZE - 1; ++i) {
                    slab[i].next = &slab[i + 1];
                }
                slab[CHUNK_SIZE - 1].next = nullptr;

                if (!free_head) {
                    free_head = slab;
                    free_tail = &slab[CHUNK_SIZE - 1];
                } else {
                    free_tail->next = slab;
                    free_tail = &slab[CHUNK_SIZE - 1];
                }
            } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl;
            }
        }

    public:
        slab_mempool() { grow(); }

        ~slab_mempool() {
            for (auto* s : slabs) delete[] s;
        }

        q_node<T>* alloc() noexcept {
            if (!free_head) grow();
            q_node<T>* node = free_head;
            free_head = node->next;
            if (!free_head) free_tail = nullptr;
            node->prev = node->next = nullptr;
            node->owning_queue = nullptr;
            return node;
        }

        void free(q_node<T>* node) noexcept {
            node->prev = node->next = nullptr;
            node->owning_queue = nullptr;
            if (!free_head) {
                free_head = free_tail = node;
            } else {
                free_tail->next = node;
                free_tail = node;
            }
        }
    };

    template<typename T>
    class queue {
        q_node<T>* head = nullptr;
        q_node<T>* tail = nullptr;
        slab_mempool<T>& mempool;

    public:
        explicit queue(slab_mempool<T>& mp) : mempool(mp) {}

        queue(queue&& q)  noexcept : mempool(q.mempool) {
            this->head = q.head;
            this->tail = q.tail;
        }

        void unlink(q_node<T>* node) noexcept {
            if (node->prev) node->prev->next = node->next;
            else head = node->next;

            if (node->next) node->next->prev = node->prev;
            else tail = node->prev;

            node->prev = node->next = nullptr;
            node->owning_queue = nullptr;
        }

        void remove_node(q_node<T>* node) noexcept {
            node->destruct();
            unlink(node);
            mempool.free(node);
        }

        q_node<T>* enqueue(const T& val) noexcept {
            q_node<T>* node = mempool.alloc();
            node->construct(val);
            node->owning_queue = this;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
            else head = node;
            tail = node;
            return node;
        }

        q_node<T>* enqueue(T&& val) noexcept {
            q_node<T>* node = mempool.alloc();
            node->construct(std::move(val));
            node->owning_queue = this;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
            else head = node;
            tail = node;
            return node;
        }

        q_node<T>* enqueue(q_node<T>&& node) noexcept {
            node.owning_queue = this;
            node.prev = tail;
            node.next = nullptr;
            if (tail) tail->next = &node;
            else head = &node;
            tail = &node;
            return &node;
        }

        [[nodiscard]] bool empty() const noexcept { return head == nullptr; }

        T dequeue() noexcept {
            // предполагаем, что очередь не пуста (для perf)
            q_node<T>* node = head;
            T val = std::forward<T>(*node->data());
            remove_node(node);
            return val;
        }

        q_node<T>&& pop() noexcept {
            q_node<T>* node = head;
            unlink(node);
            return std::move(*node);
        }
    };

    template<typename T>
    bool q_node<T>::remove() noexcept {
        if (not owning_queue) [[unlikely]] return false;
        owning_queue->remove_node(this);
        return true;
    }
}

#endif // ACE_COMMON_QUEUE_H
