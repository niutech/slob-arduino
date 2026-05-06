#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>
#include <string>

#if defined(ESP32)
#include <esp_heap_caps.h>
#define MALLOC_PSRAM(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define REALLOC_PSRAM(ptr, size) heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define FREE_PSRAM(ptr) heap_caps_free(ptr)
#else
#define MALLOC_PSRAM(size) malloc(size)
#define REALLOC_PSRAM(ptr, size) realloc(ptr, size)
#define FREE_PSRAM(ptr) free(ptr)
#endif

struct BlobRef {
    std::string key;
    uint32_t bin_index;
    uint16_t item_index;
    std::string fragment;
};

class Slob {
public:
    Slob(fs::File file);
    ~Slob();

    bool begin();
    uint32_t refCount() const { return _ref_count; }

    // Fast binary search - avoids heap allocations entirely
    int32_t find(const char* key);

    // Fetch a reference by its dictionary index
    BlobRef getRef(uint32_t index);

    // ZERO-COPY fetch: returns a direct pointer to the decompressed data
    // IMPORTANT: 
    // 1. DO NOT FREE() this pointer! It is managed by the Slob cache.
    // 2. The pointer is NOT null-terminated. You MUST use out_len to read it.
    // 3. The pointer is invalidated the next time getBlob() reads a DIFFERENT bin.
    const uint8_t* getBlob(const BlobRef& ref, uint32_t& out_len, std::string& out_type);

private:
    fs::File _f;
    
    std::string _encoding;
    std::string _compression;
    std::vector<std::string> _content_types;

    uint32_t _blob_count;
    uint32_t _ref_count;
    uint32_t _bin_count;

    uint64_t _refs_pos_offset;
    uint64_t _refs_data_offset;
    uint64_t _store_pos_offset;
    uint64_t _store_data_offset;

    uint32_t _cached_bin_index = 0xFFFFFFFF;
    uint32_t _cached_bin_item_count = 0;
    uint8_t* _cached_bin_data = nullptr;
    uint32_t _cached_bin_size = 0;
    uint8_t* _cached_content_types = nullptr;

    bool readStoreItem(uint32_t bin_index);
    bool decompressZlib(const uint8_t* in, uint32_t in_size, uint8_t*& out, uint32_t& out_size);

    uint8_t  readByte();
    uint16_t readShort();
    uint32_t readInt();
    uint64_t readLong();
    std::string readTinyText();
    std::string readText();
    uint32_t getU32(const uint8_t* p);
};