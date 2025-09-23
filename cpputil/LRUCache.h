#ifndef HV_LRU_CACHE_H_
#define HV_LRU_CACHE_H_

#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <functional>

namespace hv {

/**
 * @brief Thread-safe LRU (Least Recently Used) Cache template
 * 
 * This template provides a generic LRU cache implementation with the following features:
 * - Thread-safe operations using mutex
 * - Configurable capacity with automatic eviction
 * - O(1) get, put, and remove operations
 * - Optional eviction callback for cleanup
 * 
 * @tparam Key The key type (must be hashable)
 * @tparam Value The value type
 */
template<typename Key, typename Value>
class LRUCache {
public:
    using key_type = Key;
    using value_type = Value;
    using eviction_callback_t = std::function<void(const Key&, const Value&)>;

private:
    // Double-linked list node for LRU ordering
    struct Node {
        Key key;
        Value value;
        
        Node(const Key& k, const Value& v) : key(k), value(v) {}
    };
    
    using node_list_t = std::list<Node>;
    using node_iterator_t = typename node_list_t::iterator;
    using hash_map_t = std::unordered_map<Key, node_iterator_t>;

public:
    /**
     * @brief Construct LRUCache with specified capacity
     * @param capacity Maximum number of items to cache (default: 100)
     */
    explicit LRUCache(size_t capacity = 100) 
        : capacity_(capacity), eviction_callback_(nullptr) {
        if (capacity_ == 0) {
            capacity_ = 1; // Minimum capacity of 1
        }
    }

    /**
     * @brief Destructor
     */
    virtual ~LRUCache() {
        clear();
    }

    // Disable copy constructor and assignment operator
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    /**
     * @brief Set eviction callback function
     * @param callback Function to call when items are evicted
     */
    void set_eviction_callback(eviction_callback_t callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        eviction_callback_ = callback;
    }

    /**
     * @brief Get value by key
     * @param key The key to search for
     * @param value Output parameter for the value
     * @return true if key exists, false otherwise
     */
    bool get(const Key& key, Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = hash_map_.find(key);
        if (it == hash_map_.end()) {
            return false;
        }
        
        // Move to front (most recently used)
        move_to_front(it->second);
        value = it->second->value;
        return true;
    }

    /**
     * @brief Get value by key (alternative interface)
     * @param key The key to search for
     * @return Pointer to value if exists, nullptr otherwise
     */
    Value* get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = hash_map_.find(key);
        if (it == hash_map_.end()) {
            return nullptr;
        }
        
        // Move to front (most recently used)
        move_to_front(it->second);
        return &(it->second->value);
    }

    /**
     * @brief Put key-value pair into cache
     * @param key The key
     * @param value The value
     * @return true if new item was added, false if existing item was updated
     */
    bool put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = hash_map_.find(key);
        
        if (it != hash_map_.end()) {
            // Update existing item
            it->second->value = value;
            move_to_front(it->second);
            return false;
        }
        
        // Add new item
        if (node_list_.size() >= capacity_) {
            evict_lru();
        }
        
        node_list_.emplace_front(key, value);
        hash_map_[key] = node_list_.begin();
        return true;
    }

    /**
     * @brief Remove item by key
     * @param key The key to remove
     * @return true if item was removed, false if key not found
     */
    bool remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = hash_map_.find(key);
        if (it == hash_map_.end()) {
            return false;
        }
        
        // Call eviction callback if set
        if (eviction_callback_) {
            eviction_callback_(it->second->key, it->second->value);
        }
        
        node_list_.erase(it->second);
        hash_map_.erase(it);
        return true;
    }

    /**
     * @brief Check if key exists in cache
     * @param key The key to check
     * @return true if key exists, false otherwise
     */
    bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hash_map_.find(key) != hash_map_.end();
    }

    /**
     * @brief Clear all items from cache
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (eviction_callback_) {
            for (const auto& node : node_list_) {
                eviction_callback_(node.key, node.value);
            }
        }
        node_list_.clear();
        hash_map_.clear();
    }

    /**
     * @brief Get current cache size
     * @return Number of items in cache
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return node_list_.size();
    }

    /**
     * @brief Get cache capacity
     * @return Maximum number of items cache can hold
     */
    size_t capacity() const {
        return capacity_;
    }

    /**
     * @brief Check if cache is empty
     * @return true if cache is empty, false otherwise
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return node_list_.empty();
    }

    /**
     * @brief Set new capacity (may trigger eviction)
     * @param new_capacity New capacity value
     */
    void set_capacity(size_t new_capacity) {
        if (new_capacity == 0) {
            new_capacity = 1; // Minimum capacity of 1
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = new_capacity;
        
        // Evict excess items if necessary
        while (node_list_.size() > capacity_) {
            evict_lru();
        }
    }

    /**
     * @brief Apply a function to all cached items (for iteration)
     * @param func Function to apply to each key-value pair
     * Note: This is provided for compatibility but should be used carefully
     * as it may affect performance due to locking
     */
    template<typename Func>
    void for_each(Func func) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& node : node_list_) {
            func(node.key, node.value);
        }
    }

    /**
     * @brief Remove items that match a predicate
     * @param predicate Function that returns true for items to remove
     * @return Number of items removed
     */
    template<typename Predicate>
    size_t remove_if(Predicate predicate) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t removed_count = 0;
        
        auto it = node_list_.begin();
        while (it != node_list_.end()) {
            if (predicate(it->key, it->value)) {
                // Call eviction callback if set
                if (eviction_callback_) {
                    eviction_callback_(it->key, it->value);
                }
                
                hash_map_.erase(it->key);
                it = node_list_.erase(it);
                removed_count++;
            } else {
                ++it;
            }
        }
        
        return removed_count;
    }

protected:
    /**
     * @brief Move node to front of list (most recently used position)
     * @param it Iterator to the node to move
     */
    void move_to_front(node_iterator_t it) {
        if (it != node_list_.begin()) {
            node_list_.splice(node_list_.begin(), node_list_, it);
        }
    }

    /**
     * @brief Evict least recently used item
     */
    void evict_lru() {
        if (node_list_.empty()) {
            return;
        }
        
        auto last = std::prev(node_list_.end());
        
        // Call eviction callback if set
        if (eviction_callback_) {
            eviction_callback_(last->key, last->value);
        }
        
        hash_map_.erase(last->key);
        node_list_.erase(last);
    }

protected:
    size_t capacity_;                           // Maximum cache capacity
    mutable std::mutex mutex_;                  // Mutex for thread safety
    node_list_t node_list_;                     // Doubly-linked list for LRU ordering
    hash_map_t hash_map_;                       // Hash map for O(1) access
    eviction_callback_t eviction_callback_;     // Optional eviction callback
};

} // namespace hv

#endif // HV_LRU_CACHE_H_