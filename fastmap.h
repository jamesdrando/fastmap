#ifndef FASTMAP_H
#define FASTMAP_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// ============================================================================
// SECTION 1: GENERIC HASHING (Wyhash & Type Selection)
// ============================================================================

static inline uint64_t fm_wymix(uint64_t A, uint64_t B) {
#if defined(__SIZEOF_INT128__)
    unsigned __int128 res = (unsigned __int128)A * B;
    return (uint64_t)res ^ (uint64_t)(res >> 64);
#else
    uint64_t lo = A * B;
    uint64_t hi = (A >> 32) * (B >> 32);
    return lo ^ hi;
#endif
}

// The Universal Hash Function (Raw Bytes)
static inline uint64_t fm_hash(const void* key, size_t len) {
    const uint8_t* p = (const uint8_t*)key;
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    uint64_t see1 = len;
    while (len >= 8) {
        uint64_t v; memcpy(&v, p, 8);
        seed = fm_wymix(seed ^ v, 0xbf58476d1ce4e5b9ULL);
        p += 8; len -= 8;
    }
    if (len > 0) {
        uint64_t t = 0;
        switch (len) {
            case 7: t |= ((uint64_t)p[6]) << 48; // Fallthrough...
            case 6: t |= ((uint64_t)p[5]) << 40; // Fallthrough...
            case 5: t |= ((uint64_t)p[4]) << 32; // Fallthrough...
            case 4: t |= ((uint64_t)p[3]) << 24; // Fallthrough...
            case 3: t |= ((uint64_t)p[2]) << 16; // Fallthrough...
            case 2: t |= ((uint64_t)p[1]) << 8;  // Fallthrough...
            case 1: t |= ((uint64_t)p[0]);
        }
        seed = fm_wymix(seed ^ t, 0x94d049bb133111ebULL);
    }
    return fm_wymix(seed, see1);
}

// --- Type-Generic Hash Helpers ---

#define FM_MAKE_HASH_FN(type, suffix) \
    static inline uint64_t fm_hash_##suffix(type val) { \
        return fm_hash(&val, sizeof(val)); \
    }

FM_MAKE_HASH_FN(char,               char)
FM_MAKE_HASH_FN(signed char,        schar)
FM_MAKE_HASH_FN(unsigned char,      uchar)
FM_MAKE_HASH_FN(short,              short)
FM_MAKE_HASH_FN(unsigned short,     ushort)
FM_MAKE_HASH_FN(int,                int)
FM_MAKE_HASH_FN(unsigned int,       uint)
FM_MAKE_HASH_FN(long,               long)
FM_MAKE_HASH_FN(unsigned long,      ulong)
FM_MAKE_HASH_FN(long long,          llong)
FM_MAKE_HASH_FN(unsigned long long, ullong)

// Special: String Content Hashing
static inline uint64_t fm_hash_str(const char* str) {
    return fm_hash(str, strlen(str));
}

// Special: Float Hashing (Normalize -0.0 to 0.0)
static inline uint64_t fm_hash_float(float val) {
    if (val == 0.0f) val = 0.0f; 
    return fm_hash(&val, sizeof(val));
}
static inline uint64_t fm_hash_double(double val) {
    if (val == 0.0) val = 0.0; 
    return fm_hash(&val, sizeof(val));
}

// The Generic Selector
#define fm_hash_val(X) _Generic((X), \
    _Bool:              fm_hash_uchar,   \
    char:               fm_hash_char,    \
    signed char:        fm_hash_schar,   \
    unsigned char:      fm_hash_uchar,   \
    short:              fm_hash_short,   \
    unsigned short:     fm_hash_ushort,  \
    int:                fm_hash_int,     \
    unsigned int:       fm_hash_uint,    \
    long:               fm_hash_long,    \
    unsigned long:      fm_hash_ulong,   \
    long long:          fm_hash_llong,   \
    unsigned long long: fm_hash_ullong,  \
    float:              fm_hash_float,   \
    double:             fm_hash_double,  \
    char*:              fm_hash_str,     \
    const char*:        fm_hash_str      \
)(X)

// ============================================================================
// SECTION 2: INTERNAL DYNAMIC ARRAY (Type Erased)
// ============================================================================

typedef struct {
    unsigned char* data;
    size_t length;
    size_t capacity;
    size_t stride;
} fm_vector;

static inline void fm_vec_init(fm_vector* vec, size_t stride, size_t cap) {
    vec->data = (unsigned char*)calloc(cap, stride); // calloc zeroes memory
    vec->length = 0;
    vec->capacity = cap;
    vec->stride = stride;
}

static inline void fm_vec_grow(fm_vector* vec) {
    size_t new_cap = vec->capacity == 0 ? 8 : vec->capacity * 2;
    unsigned char* new_data = (unsigned char*)realloc(vec->data, new_cap * vec->stride);
    if (!new_data) abort(); // Handle OOM
    vec->data = new_data;
    vec->capacity = new_cap;
}

static inline void fm_vec_push(fm_vector* vec, const void* item) {
    if (vec->length >= vec->capacity) fm_vec_grow(vec);
    memcpy(vec->data + (vec->length * vec->stride), item, vec->stride);
    vec->length++;
}

static inline void* fm_vec_at(fm_vector* vec, size_t index) {
    return vec->data + (index * vec->stride);
}

static inline void fm_vec_free(fm_vector* vec) {
    free(vec->data);
    vec->data = NULL;
    vec->length = 0;
}

// ============================================================================
// SECTION 3: THE DENSE MAP STRUCTURE
// ============================================================================

// Special index to mark a bucket as empty
#define FM_EMPTY_IDX 0xFFFFFFFF

typedef struct {
    // The Dense Storage
    fm_vector keys;    // User's Keys
    fm_vector values;  // User's Values
    fm_vector hashes;  // Cached uint64_t hashes (avoids re-hashing on resize)

    // The Sparse Index (The "Buckets")
    // This array stores indices into the vectors above.
    uint32_t* buckets; 
    size_t bucket_count; 
    size_t bucket_mask;  // Optimization: size - 1 (for fast modulo)
    
    // Metadata
    size_t key_size;
    size_t val_size;
    float max_load_factor; // e.g., 0.75
} _FastMap;

// Initialize the map
static inline _FastMap fm_init(size_t key_size, size_t val_size) {
    _FastMap map;
    map.key_size = key_size;
    map.val_size = val_size;
    map.max_load_factor = 0.80f; // Dense maps can handle high load
    map.bucket_count = 16;       // Power of 2 start
    map.bucket_mask = 15;
    
    // Alloc buckets (init to EMPTY)
    map.buckets = (uint32_t*)malloc(map.bucket_count * sizeof(uint32_t));
    memset(map.buckets, 0xFF, map.bucket_count * sizeof(uint32_t)); // Set to -1

    // Init vectors
    fm_vec_init(&map.keys, key_size, 8);
    fm_vec_init(&map.values, val_size, 8);
    fm_vec_init(&map.hashes, sizeof(uint64_t), 8);

    return map;
}

static inline void fm_free(_FastMap* map) {
    fm_vec_free(&map->keys);
    fm_vec_free(&map->values);
    fm_vec_free(&map->hashes);
    free(map->buckets);
}

// ============================================================================
// SECTION 4: INTERNAL LOGIC (Resize & Robin Hood)
// ============================================================================

// Place an index into the bucket array using Robin Hood Hashing
static inline void fm_place_index(uint32_t* buckets, size_t mask, uint64_t hash, uint32_t vec_idx, const fm_vector* hashes_vec) {
    size_t bucket_idx = hash & mask;
    uint32_t dist = 0;

    while (true) {
        uint32_t existing_idx = buckets[bucket_idx];

        // Case 1: Empty Slot - Found our home!
        if (existing_idx == FM_EMPTY_IDX) {
            buckets[bucket_idx] = vec_idx;
            return;
        }

        // Case 2: Collision - ROBIN HOOD SWAP
        // We need to check if the existing item is "richer" (closer to home) than us.
        
        // Retrieve the hash of the item currently sitting here
        uint64_t existing_hash = *(uint64_t*)fm_vec_at((fm_vector*)hashes_vec, existing_idx);
        
        // Calculate its current distance from its ideal home
        size_t ideal_idx = existing_hash & mask;
        // Handle wrapping logic for distance
        uint32_t existing_dist = (bucket_idx + mask + 1 - ideal_idx) & mask;

        if (existing_dist < dist) {
            // SWAP! We are poorer (further away), so we steal this spot.
            // The existing guy gets evicted and has to find a new spot.
            uint32_t temp = buckets[bucket_idx];
            buckets[bucket_idx] = vec_idx;
            vec_idx = temp;
            
            dist = existing_dist; // Update distance for the evicted item
        }

        // Move to next bucket (Linear Probe)
        bucket_idx = (bucket_idx + 1) & mask;
        dist++;
    }
}

static inline void fm_resize(_FastMap* map, size_t new_capacity) {
    uint32_t* new_buckets = (uint32_t*)malloc(new_capacity * sizeof(uint32_t));
    memset(new_buckets, 0xFF, new_capacity * sizeof(uint32_t)); // Set to -1
    
    size_t new_mask = new_capacity - 1;
    
    // Re-insert every existing item into the new bucket array
    for (size_t i = 0; i < map->keys.length; i++) {
        uint64_t h = *(uint64_t*)fm_vec_at(&map->hashes, i);
        fm_place_index(new_buckets, new_mask, h, (uint32_t)i, &map->hashes);
    }

    free(map->buckets);
    map->buckets = new_buckets;
    map->bucket_count = new_capacity;
    map->bucket_mask = new_mask;
}

// ============================================================================
// SECTION 5: PUBLIC API (Put / Get / Delete)
// ============================================================================

// Insert or Update
static inline void fm_put(_FastMap* map, const void* key, const void* value) {
    // 1. Check Load Factor
    if (map->keys.length >= map->bucket_count * map->max_load_factor) {
        fm_resize(map, map->bucket_count * 2);
    }

    uint64_t hash = fm_hash(key, map->key_size);
    size_t bucket_idx = hash & map->bucket_mask;
    size_t dist = 0;

    // 2. Probe to see if key exists
    while (true) {
        uint32_t idx = map->buckets[bucket_idx];

        // Stop if empty (Key doesn't exist, insert new)
        if (idx == FM_EMPTY_IDX) break;

        // Robin Hood Optimization: Early Exit on Miss
        uint64_t existing_hash = *(uint64_t*)fm_vec_at(&map->hashes, idx);
        size_t ideal_idx = existing_hash & map->bucket_mask;
        uint32_t existing_dist = (bucket_idx + map->bucket_mask + 1 - ideal_idx) & map->bucket_mask;
        if (existing_dist < dist) break; // We can stop probing

        // Check for Match
        void* existing_key = fm_vec_at(&map->keys, idx);
        if (memcmp(existing_key, key, map->key_size) == 0) {
            // Update Value
            void* val_ptr = fm_vec_at(&map->values, idx);
            memcpy(val_ptr, value, map->val_size);
            return; 
        }

        bucket_idx = (bucket_idx + 1) & map->bucket_mask;
        dist++;
    }

    // 3. Insert New (Append to dense vectors)
    uint32_t new_idx = (uint32_t)map->keys.length;
    fm_vec_push(&map->keys, key);
    fm_vec_push(&map->values, value);
    fm_vec_push(&map->hashes, &hash); // Cache the hash!

    // 4. Place index into buckets (Robin Hood logic handles the rest)
    fm_place_index(map->buckets, map->bucket_mask, hash, new_idx, &map->hashes);
}

// Get Value
static inline void* fm_get(_FastMap* map, const void* key) {
    uint64_t hash = fm_hash(key, map->key_size);
    size_t bucket_idx = hash & map->bucket_mask;
    size_t dist = 0; // Track our distance for early exit

    while (true) {
        uint32_t idx = map->buckets[bucket_idx];
        
        if (idx == FM_EMPTY_IDX) return NULL; // Not found

        // Check Hash & Key
        void* existing_key = fm_vec_at(&map->keys, idx);
        if (memcmp(existing_key, key, map->key_size) == 0) {
            return fm_vec_at(&map->values, idx);
        }

        // Robin Hood Early Exit
        uint64_t existing_hash = *(uint64_t*)fm_vec_at(&map->hashes, idx);
        size_t ideal_idx = existing_hash & map->bucket_mask;
        uint32_t existing_dist = (bucket_idx + map->bucket_mask + 1 - ideal_idx) & map->bucket_mask;
        
        if (existing_dist < dist) return NULL; // Impossible to be further down

        bucket_idx = (bucket_idx + 1) & map->bucket_mask;
        dist++;
    }
}

// Helper: updates the bucket that points to a specific vector index
static inline void fm_update_bucket_for_moved_item(_FastMap* map, uint32_t old_vec_idx, uint32_t new_vec_idx) {
    // We have to find the bucket pointing to old_vec_idx and update it.
    // To do this fast, we use the stored hash of the MOVED item.
    
    uint64_t hash = *(uint64_t*)fm_vec_at(&map->hashes, new_vec_idx);
    size_t bucket_idx = hash & map->bucket_mask;

    while (true) {
        if (map->buckets[bucket_idx] == old_vec_idx) {
            map->buckets[bucket_idx] = new_vec_idx;
            return;
        }
        bucket_idx = (bucket_idx + 1) & map->bucket_mask;
    }
}

// The Delete Function
static inline bool fm_erase(_FastMap* map, const void* key) {
    uint64_t hash = fm_hash(key, map->key_size);
    size_t bucket_idx = hash & map->bucket_mask;
    size_t dist = 0;

    while (true) {
        uint32_t vec_idx = map->buckets[bucket_idx];
        
        // 1. Not Found (Empty or Early Exit)
        if (vec_idx == FM_EMPTY_IDX) return false;

        // Robin Hood Early Exit Check
        uint64_t existing_hash = *(uint64_t*)fm_vec_at(&map->hashes, vec_idx);
        size_t ideal_idx = existing_hash & map->bucket_mask;
        uint32_t existing_dist = (bucket_idx + map->bucket_mask + 1 - ideal_idx) & map->bucket_mask;
        if (existing_dist < dist) return false;

        // 2. Found Match?
        void* current_key = fm_vec_at(&map->keys, vec_idx);
        if (memcmp(current_key, key, map->key_size) == 0) {
            // === FOUND IT. DELETE LOGIC STARTS ===

            // A. SWAP-AND-POP from Vectors
            // We move the LAST item in the vector into this slot to fill the hole.
            uint32_t last_vec_idx = (uint32_t)map->keys.length - 1;
            
            if (vec_idx != last_vec_idx) {
                // Move Key
                void* dst_k = fm_vec_at(&map->keys, vec_idx);
                void* src_k = fm_vec_at(&map->keys, last_vec_idx);
                memcpy(dst_k, src_k, map->key_size);

                // Move Value
                void* dst_v = fm_vec_at(&map->values, vec_idx);
                void* src_v = fm_vec_at(&map->values, last_vec_idx);
                memcpy(dst_v, src_v, map->val_size);

                // Move Hash
                void* dst_h = fm_vec_at(&map->hashes, vec_idx);
                void* src_h = fm_vec_at(&map->hashes, last_vec_idx);
                memcpy(dst_h, src_h, sizeof(uint64_t));

                // CRITICAL: The bucket that pointed to 'last_vec_idx' implies it is
                // strictly pointing to the end. We must find that bucket and update 
                // it to point to 'vec_idx' (the new location).
                fm_update_bucket_for_moved_item(map, last_vec_idx, vec_idx);
            }

            // Decrease size (Pop)
            map->keys.length--;
            map->values.length--;
            map->hashes.length--;

            // B. BACKSHIFT DELETION in Buckets
            // The current 'bucket_idx' is now effectively "empty".
            // We must fill it by shifting neighboring items back if they are probing.
            
            size_t hole_idx = bucket_idx;
            size_t next_idx = (hole_idx + 1) & map->bucket_mask;

            while (true) {
                uint32_t next_val = map->buckets[next_idx];
                
                // If next slot is empty, we are done. The hole is at the end of the chain.
                if (next_val == FM_EMPTY_IDX) {
                    map->buckets[hole_idx] = FM_EMPTY_IDX;
                    return true;
                }

                // Calculate where 'next_val' inherently WANTS to be.
                uint64_t next_hash = *(uint64_t*)fm_vec_at(&map->hashes, next_val);
                size_t ideal_idx = next_hash & map->bucket_mask;

                // Check if 'next_val' is currently shifted to the right of 'hole_idx'.
                // (This logic handles the wrap-around case)
                size_t dist_to_hole = (hole_idx + map->bucket_count - ideal_idx) & map->bucket_mask;
                size_t dist_to_next = (next_idx + map->bucket_count - ideal_idx) & map->bucket_mask;

                if (dist_to_hole < dist_to_next) {
                    // The item at 'next_idx' is probing and CAN fit into 'hole_idx'.
                    // Move it back!
                    map->buckets[hole_idx] = next_val;
                    hole_idx = next_idx; // The hole moves forward
                } else {
                    // The item is happy (or blocked by ideal position). 
                    // We cannot move it. The hole stays here? 
                    // Actually in Robin Hood, we just continue scanning.
                }

                next_idx = (next_idx + 1) & map->bucket_mask;
            }
            return true;
        }

        bucket_idx = (bucket_idx + 1) & map->bucket_mask;
        dist++;
    }
}

// ============================================================================
// SECTION 6: HELPERS, MACROS & API STRUCT
// ============================================================================

// Helper to initialize map with types
// _FastMap map = FM_INIT(int, float);
#define FM_INIT(K, V) fm_init(sizeof(K), sizeof(V))

// Helper to put literals
// FM_PUT(&map, int, 10, float, 55.5f);
#define FM_PUT(map_ptr, KType, k, VType, v) do { \
    KType _k = (k); VType _v = (v); \
    fm_put((map_ptr), &_k, &_v); \
} while(0)

// Helper to get value
// FIXED: Now returns void* directly, allowing implicit cast to int* or any type.
#define FM_GET(map_ptr, KType, k) \
    (fm_get((map_ptr), &((KType){k})))

// Helper to delete value
#define FM_DELETE(map_ptr, KType, k) \
    fm_erase((map_ptr), &((KType){k}))

// ----------------------------------------------------------------------------
// THE 'fm' NAMESPACE STRUCT
// Allows syntax like: fm.put(&map, &key, &val);
// Note: Uses raw pointers, does not handle literals automatically.
// ----------------------------------------------------------------------------
typedef struct {
    _FastMap (*init)(size_t, size_t);
    void (*put)(_FastMap*, const void*, const void*);
    void* (*get)(_FastMap*, const void*);
    bool (*del)(_FastMap*, const void*);
    void (*free)(_FastMap*);
} FastMap;

static const FastMap fm = {
    .init = fm_init,
    .put = fm_put,
    .get = fm_get,
    .del = fm_erase,
    .free = fm_free
};

#endif // FASTMAP_H