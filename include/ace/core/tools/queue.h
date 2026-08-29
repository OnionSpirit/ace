/**
 * @file queue.h
 * @brief Intrusive doubly-linked list queue with slab memory pool and node
 *        ejection support.
 *
 * @details Unlike the lock-free nukes queues, this queue is single-threaded and
 * designed for use inside a single runner.  Its key feature is @c q_node::remove()
 * — a node can unlink itself from the queue in O(1) without knowing its position.
 * This is used by the clock's @c hierarchical_time_wheel for timer cancellation.
 *
 * Components:
 *  - @c q_node<T> — doubly-linked node with in-place storage for @c T.
 *  - @c slab_mempool<T> — pre-allocated slab allocator for nodes.
 *  - @c queue<T> — intrusive doubly-linked FIFO queue.
 */
#ifndef ACE_COMMON_QUEUE_H
#define ACE_COMMON_QUEUE_H

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

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
        q_node* prev = nullptr;          ///< Previous node in the queue.
        q_node* next = nullptr;          ///< Next node in the queue.
        queue<T>* owning_queue = nullptr; ///< Queue this node currently belongs to.

        alignas(T) unsigned char storage[sizeof(T)]{}; ///< In-place aligned storage for @c T.

        /// @brief Mutable access to the stored element.
        T* data() noexcept { return reinterpret_cast<T*>(storage); }
        /// @brief Const access to the stored element.
        [[nodiscard]] const T* data() const noexcept { return reinterpret_cast<const T*>(storage); }

        /// @brief Placement-constructs the element from a copy.
        /// @param val Value to copy into storage.
        void construct(const T& val) noexcept(std::is_nothrow_copy_constructible_v<T>) {
            new (storage) T(val);
        }
        /// @brief Placement-constructs the element by move.
        /// @param val Value to move into storage.
        void construct(T&& val) noexcept(std::is_nothrow_move_constructible_v<T>) {
            new (storage) T(std::move(val));
        }
        /// @brief Destroys the stored element.
        void destruct() noexcept { data()->~T(); }

        /**
         * @brief Unlinks this node from its owning queue in O(1).
         * @return @c true if the node was removed, @c false if it is not queued.
         */
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
        q_node<T>* free_head = nullptr;          ///< Head of the free-node list.
        q_node<T>* free_tail = nullptr;          ///< Tail of the free-node list.
        std::vector<q_node<T>*> slabs;           ///< All allocated slabs (for destruction).
        static constexpr size_t CHUNK_SIZE = 1024; ///< Nodes per slab allocation.

        /**
         * @brief Allocates a new slab and links its nodes into the free list.
         */
        void grow() {
            auto slab_owner = std::make_unique<q_node<T>[]>(CHUNK_SIZE);
            q_node<T>* slab = slab_owner.get();
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
            (void)slab_owner.release();
        }

    public:
        /// @brief Constructs the pool with one pre-allocated slab.
        slab_mempool() { grow(); }

        /// @brief Destroys the pool, freeing all slabs.
        ~slab_mempool() {
            for (auto* s : slabs) delete[] s;
        }

        /**
         * @brief Takes a node from the free list, growing the pool if empty.
         * @return A cleared node ready for use.
         */
        q_node<T>* alloc() {
            if (!free_head) grow();
            q_node<T>* node = free_head;
            free_head = node->next;
            if (!free_head) free_tail = nullptr;
            node->prev = node->next = nullptr;
            node->owning_queue = nullptr;
            return node;
        }

        /**
         * @brief Returns a node to the free list.
         * @param node Node to release.
         */
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
        static_assert(std::is_nothrow_destructible_v<T>,
            "queue payload destructors must be noexcept");
        q_node<T>* head = nullptr;          ///< First node of the queue.
        q_node<T>* tail = nullptr;          ///< Last node of the queue.
        slab_mempool<T>& mempool;           ///< Shared node allocator.

    public:
        /**
         * @brief Binds the queue to a shared node pool.
         * @param mp The slab pool to allocate nodes from.
         */
        explicit queue(slab_mempool<T>& mp) : mempool(mp) {}

        /**
         * @brief Move constructor — transfers the nodes and nulls the source.
         * @param q Source queue to move from.
         */
        queue(queue&& q)  noexcept : mempool(q.mempool) {
            this->head = q.head;
            this->tail = q.tail;
            q.head = nullptr;
            q.tail = nullptr;
        }

        /**
         * @brief Detaches a node without destroying its element.
         * @param node Node to unlink.
         */
        void unlink(q_node<T>* node) noexcept {
            if (node->prev) node->prev->next = node->next;
            else head = node->next;

            if (node->next) node->next->prev = node->prev;
            else tail = node->prev;

            node->prev = node->next = nullptr;
            node->owning_queue = nullptr;
        }

        /**
         * @brief Destroys, unlinks and frees a node in one step.
         * @param node Node to remove.
         */
        void remove_node(q_node<T>* node) noexcept {
            node->destruct();
            unlink(node);
            mempool.free(node);
        }

        /**
         * @brief Appends a copied element to the queue.
         * @param val Element to copy.
         * @return The newly enqueued node.
         */
        q_node<T>* enqueue(const T& val) {
            q_node<T>* node = mempool.alloc();
            try {
                node->construct(val);
            } catch (...) {
                mempool.free(node);
                throw;
            }
            node->owning_queue = this;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
            else head = node;
            tail = node;
            return node;
        }

        /**
         * @brief Appends a moved element to the queue.
         * @param val Element to move.
         * @return The newly enqueued node.
         */
        q_node<T>* enqueue(T&& val) {
            q_node<T>* node = mempool.alloc();
            try {
                node->construct(std::move(val));
            } catch (...) {
                mempool.free(node);
                throw;
            }
            node->owning_queue = this;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
            else head = node;
            tail = node;
            return node;
        }

        /**
         * @brief Appends an already-constructed node.
         * @param node Node to take ownership of.
         * @return Pointer to the enqueued node.
         */
        q_node<T>* enqueue(q_node<T>&& node) noexcept {
            node.owning_queue = this;
            node.prev = tail;
            node.next = nullptr;
            if (tail) tail->next = &node;
            else head = &node;
            tail = &node;
            return &node;
        }

        /// @brief @c true when the queue holds no nodes.
        [[nodiscard]] bool empty() const noexcept { return head == nullptr; }

        /**
         * @brief Removes the head element and returns its value.
         * @warning Assumes the queue is not empty (for performance).
         * @return The head element.
         */
        T dequeue() noexcept(std::is_nothrow_move_constructible_v<T>) {
            // NOTE: Assumes queue is not empty (for performance)
            q_node<T>* node = head;
            T val = std::forward<T>(*node->data());
            remove_node(node);
            return val;
        }

        /**
         * @brief Unlinks the head node without destroying its element.
         * @return The unlinked node (moved-out).
         */
        q_node<T>&& pop() noexcept {
            q_node<T>* node = head;
            unlink(node);
            return std::move(*node);
        }
    };

    template<typename T>
    /**
     * @brief Unlinks this node from its owning queue.
     * @return @c false when the node is not owned by any queue.
     */
    bool q_node<T>::remove() noexcept {
        if (not owning_queue) [[unlikely]] return false;
        owning_queue->remove_node(this);
        return true;
    }
}

#endif // ACE_COMMON_QUEUE_H
