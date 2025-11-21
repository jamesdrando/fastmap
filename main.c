#include <stdio.h>
#include <assert.h>
#include <time.h>
#include "fastmap.h"

// ============================================================================
// TEST HELPERS
// ============================================================================

#define ASSERT_EQ(expect, actual, format) \
    do { \
        if ((expect) != (actual)) { \
            fprintf(stderr, "FAILED %s:%d: Expected " format ", got " format "\n", \
                    __FILE__, __LINE__, (expect), (actual)); \
            abort(); \
        } \
    } while(0)

#define LOG_PASS(name) printf("[PASS] %s\n", name)

// ============================================================================
// GLOBAL TYPES (Must be defined before macros use them)
// ============================================================================

typedef struct {
    float x, y, z;
} Vec3;

// ============================================================================
// TEST CASES
// ============================================================================

void test_basic_int_map() {
    _FastMap map = FM_INIT(int, int);

    // 1. Insert
    FM_PUT(&map, int, 10, int, 100);
    FM_PUT(&map, int, 20, int, 200);
    FM_PUT(&map, int, 30, int, 300);

    // 2. Retrieve
    int* v1 = FM_GET(&map, int, 10);
    int* v2 = FM_GET(&map, int, 20);
    int* v3 = FM_GET(&map, int, 99); // Non-existent

    assert(v1 != NULL && *v1 == 100);
    assert(v2 != NULL && *v2 == 200);
    assert(v3 == NULL);

    // 3. Update
    FM_PUT(&map, int, 10, int, 999);
    v1 = FM_GET(&map, int, 10);
    assert(*v1 == 999);

    // 4. Delete
    bool deleted = FM_DELETE(&map, int, 20);
    assert(deleted == true);
    assert(map.keys.length == 2);
    
    v2 = FM_GET(&map, int, 20);
    assert(v2 == NULL);

    // 5. Delete Non-existent
    deleted = FM_DELETE(&map, int, 555);
    assert(deleted == false);

    fm_free(&map);
    LOG_PASS("Basic Integer Map");
}

void test_string_keys() {
    // Key is char* (pointer), Value is int
    _FastMap map = FM_INIT(char*, int);

    // Use const char* for string literals
    const char* k1 = "apple";
    const char* k2 = "banana";
    const char* k3 = "cherry";

    // 1. Insert
    FM_PUT(&map, const char*, k1, int, 1);
    FM_PUT(&map, const char*, k2, int, 2);
    FM_PUT(&map, const char*, k3, int, 3);

    // 2. Get
    // Cast literal to (char*) to satisfy strict pointer types if necessary,
    // but usually const char* works if the macro handles it. 
    // We use (char*) cast here to match the Key Type declared in FM_INIT.
    int* val = FM_GET(&map, char*, (char*)"banana"); 
    assert(val != NULL && *val == 2);

    // 3. String Hashing Check
    char buffer[16];
    strcpy(buffer, "app");
    strcat(buffer, "le"); 
    
    val = FM_GET(&map, char*, buffer);
    assert(val != NULL && *val == 1);

    fm_free(&map);
    LOG_PASS("String Content Hashing");
}

void test_struct_values() {
    _FastMap map = FM_INIT(int, Vec3);

    Vec3 v1 = {1.0f, 2.0f, 3.0f};
    FM_PUT(&map, int, 1, Vec3, v1);

    Vec3* retrieved = FM_GET(&map, int, 1);
    assert(retrieved != NULL);
    assert(retrieved->y == 2.0f);

    // Modify in place
    retrieved->z = 99.0f;

    Vec3* check = FM_GET(&map, int, 1);
    assert(check->z == 99.0f);

    fm_free(&map);
    LOG_PASS("Struct Values");
}

void test_deletion_integrity() {
    _FastMap map = FM_INIT(int, int);

    for(int i = 0; i < 5; i++) {
        FM_PUT(&map, int, i, int, i * 10);
    }

    // Delete index 0. Key 4 should move to slot 0.
    FM_DELETE(&map, int, 0); 

    assert(map.keys.length == 4);

    int* v4 = FM_GET(&map, int, 4);
    assert(v4 != NULL && *v4 == 40);

    int* v0 = FM_GET(&map, int, 0);
    assert(v0 == NULL);

    int* v2 = FM_GET(&map, int, 2);
    assert(v2 != NULL && *v2 == 20);

    fm_free(&map);
    LOG_PASS("Deletion Integrity (Swap & Pop)");
}

void test_massive_resize() {
    _FastMap map = FM_INIT(int, int);
    
    int COUNT = 100000;

    clock_t start = clock();

    // Insert
    for (int i = 0; i < COUNT; i++) {
        FM_PUT(&map, int, i, int, i);
    }
    
    // Verify Count
    assert(map.keys.length == (size_t)COUNT);

    // Verify All
    for (int i = 0; i < COUNT; i++) {
        int* val = FM_GET(&map, int, i);
        if (!val || *val != i) {
            printf("Failed at index %d\n", i);
            abort();
        }
    }

    clock_t end = clock();
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("    [PERF] Inserted & Verified %d items in %.4f sec\n", COUNT, time_taken);

    fm_free(&map);
    LOG_PASS("Massive Resize & Collision Handling");
}

int main() {
    printf("=== FastMap Test Suite ===\n");
    
    test_basic_int_map();
    test_string_keys();
    test_struct_values();
    test_deletion_integrity();
    test_massive_resize();

    printf("=== All Tests Passed ===\n");
    return 0;
}