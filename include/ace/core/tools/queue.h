/**
 * @file queue.h
 * @brief Intrusive doubly-linked list queue with slab memory pool and node
 *        ejection support.
 *
 * @details Unlike the lock-free nukes queues, this queue is single-threaded and
 * designed for use inside a single runner.  Its key feature is @c q_node::remove()
 * — a node can unlink itself from the queue in O(1) without knowing its position.
 * This is used by the clock's @c multi_dial for timer cancellation.
 *
 * Components:
 *  - @c q_node<T> — doubly-linked node with in-place storage for @c T.
 *  - @c slab_mempool<T> — pre-allocated slab allocator for nodes.
 *  - @c queue<T> — intrusive doubly-linked FIFO queue.
 */
#ifndef ACE_COMMON_QUEUE_H
#define ACE_COMMON_QUEUE_H

#include <vector>
#include <utility>

namespace ace::core::tools {

    template <typename T>
    class queue;

    /**
     * @brief Doubly-linked node with in-place aligned storage for @c T.
     *
     * @details Each node can unlink itself from its owning queue via
     * @c remove() — used by the time wheel for O(1) timer cancellation.
     *
     * @tparam T  The stored element type.
     */
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

    /**
     * @brief Slab-allocated memory pool for @c q_node<T>.
     *
     * @details Allocates memory in chunks of 1024 nodes.  Freed nodes are
     * returned to a free list for reuse.  Not thread-safe — intended for
     * single-runner use.
     *
     * @tparam T  The element type stored in the nodes.
     */
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

    /**
     * @brief Intrusive doubly-linked FIFO queue with O(1) node ejection.
     *
     * @details Nodes are allocated from a @c slab_mempool.  The key feature
     * is @c remove_node() / @c q_node::remove() — a node can detach itself
     * mid-queue in O(1), which is essential for timer cancellation in the
     * time wheel.
     *
     * @tparam T  The stored element type.
     */
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
            // NOTE: Assumes queue is not empty (for performance)
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
