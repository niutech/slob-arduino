#include "Slob.h"
#include <miniz.h>

static void* miniz_alloc(void* opaque, size_t items, size_t size) {
    return MALLOC_PSRAM(items * size);
}
static void miniz_free(void* opaque, void* ptr) {
    FREE_PSRAM(ptr);
}

Slob::Slob(fs::File file) : _f(file) {}

Slob::~Slob() {
    if (_cached_bin_data) FREE_PSRAM(_cached_bin_data);
    if (_cached_content_types) FREE_PSRAM(_cached_content_types);
    if (_f) _f.close();
}

uint8_t Slob::readByte() {
    return _f.read();
}

uint16_t Slob::readShort() {
    uint16_t v = 0;
    _f.read((uint8_t*)&v, 2);
    return __builtin_bswap16(v);
}

uint32_t Slob::readInt() {
    uint32_t v = 0;
    _f.read((uint8_t*)&v, 4);
    return __builtin_bswap32(v);
}

uint64_t Slob::readLong() {
    uint64_t v = 0;
    _f.read((uint8_t*)&v, 8);
    return __builtin_bswap64(v);
}

std::string Slob::readTinyText() {
    uint8_t len = readByte();
    if (len == 0) return "";
    std::string s(len, '\0');
    _f.read((uint8_t*)s.data(), len);
    return s;
}

std::string Slob::readText() {
    uint16_t len = readShort();
    if (len == 0) return "";
    std::string s(len, '\0');
    _f.read((uint8_t*)s.data(), len);
    return s;
}

uint32_t Slob::getU32(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

bool Slob::begin() {
    if (!_f) return false;
    _f.seek(0);
    
    uint8_t magic[8];
    if (_f.read(magic, 8) != 8) return false;
    if (memcmp(magic, "!-1SLOB\x1F", 8) != 0) return false;

    _f.seek(16, SeekCur); // Skip UUID
    _encoding = readTinyText();
    _compression = readTinyText();

    // Skip tags
    uint8_t tag_count = readByte();
    for (int i = 0; i < tag_count; ++i) {
        readTinyText(); // key
        readTinyText(); // value
    }

    // Read content types
    uint8_t ct_count = readByte();
    for (int i = 0; i < ct_count; ++i) {
        _content_types.push_back(readText());
    }

    _blob_count = readInt();
    uint64_t store_offset = readLong();
    uint64_t size = readLong(); // unused locally
    uint64_t refs_offset = _f.position();

    // Init refs offsets
    _f.seek(refs_offset);
    _ref_count = readInt();
    _refs_pos_offset = _f.position();
    _refs_data_offset = _refs_pos_offset + _ref_count * 8;

    // Init store offsets
    _f.seek(store_offset);
    _bin_count = readInt();
    _store_pos_offset = _f.position();
    _store_data_offset = _store_pos_offset + _bin_count * 8;

    return true;
}

BlobRef Slob::getRef(uint32_t index) {
    BlobRef ref;
    _f.seek(_refs_pos_offset + index * 8);
    uint64_t pos = readLong();
    
    _f.seek(_refs_data_offset + pos);
    ref.key = readText();
    ref.bin_index = readInt();
    ref.item_index = readShort();
    ref.fragment = readTinyText();
    return ref;
}

int32_t Slob::find(const char* key) {
    int32_t left = 0;
    int32_t right = _ref_count - 1;
    char stack_buf[256]; 
    
    while (left <= right) {
        int32_t mid = left + (right - left) / 2;
        
        _f.seek(_refs_pos_offset + mid * 8);
        uint64_t pos = readLong();
        
        _f.seek(_refs_data_offset + pos);
        uint16_t key_len = readShort();
        
        char* key_ptr = stack_buf;
        bool using_heap = false;

        if (key_len >= sizeof(stack_buf)) {
            key_ptr = (char*)malloc(key_len + 1);
            if (!key_ptr) return -1; // Out of memory
            using_heap = true;
        }

        _f.read((uint8_t*)key_ptr, key_len);
        key_ptr[key_len] = '\0';

        int cmp = strcasecmp(key_ptr, key);

        if (using_heap) free(key_ptr);
        
        if (cmp == 0) return mid;
        if (cmp < 0) left = mid + 1;
        else right = mid - 1;
    }
    return -1;
}

bool Slob::decompressZlib(const uint8_t* in, uint32_t in_size, uint8_t*& out, uint32_t& out_size) {
    mz_stream strm = {0};
    strm.zalloc = miniz_alloc; 
    strm.zfree = miniz_free;
    strm.opaque = MZ_NULL;

    if (mz_inflateInit(&strm) != MZ_OK) return false;
    
    strm.next_in = (const unsigned char*)in;
    strm.avail_in = in_size;

    uint32_t buf_size = 128 * 1024; // 128KB initial chunk
    out = (uint8_t*)MALLOC_PSRAM(buf_size);
    if (!out) {
        mz_inflateEnd(&strm);
        return false;
    }
    
    strm.next_out = out;
    strm.avail_out = buf_size;

    while(true) {
        int ret = mz_inflate(&strm, MZ_NO_FLUSH);
        if (ret == MZ_STREAM_END) break;
        if (ret != MZ_OK && ret != MZ_BUF_ERROR) {
            FREE_PSRAM(out);
            out = nullptr;
            mz_inflateEnd(&strm);
            return false;
        }
        
        if (strm.avail_out == 0) {
            uint32_t new_size = buf_size * 2;
            uint8_t* new_out = (uint8_t*)REALLOC_PSRAM(out, new_size);
            if (!new_out) {
                FREE_PSRAM(out);
                out = nullptr;
                mz_inflateEnd(&strm);
                return false;
            }
            out = new_out;
            strm.next_out = out + buf_size;
            strm.avail_out = buf_size;
            buf_size = new_size;
        } else if (ret == MZ_BUF_ERROR) {
            FREE_PSRAM(out);
            out = nullptr;
            mz_inflateEnd(&strm);
            return false;
        }
    }
    
    out_size = strm.total_out;
    mz_inflateEnd(&strm);

    uint8_t* shrunk_out = (uint8_t*)REALLOC_PSRAM(out, out_size);
    if (shrunk_out) out = shrunk_out;

    return true;
}

bool Slob::readStoreItem(uint32_t bin_index) {
    if (_cached_bin_index == bin_index) return true;

    _f.seek(_store_pos_offset + bin_index * 8);
    uint64_t pos = readLong();
    _f.seek(_store_data_offset + pos);

    uint32_t bin_item_count = readInt();

    if (_cached_content_types) FREE_PSRAM(_cached_content_types);
    _cached_content_types = (uint8_t*)MALLOC_PSRAM(bin_item_count);
    if (!_cached_content_types) return false;
    _f.read(_cached_content_types, bin_item_count);

    uint32_t content_length = readInt();

    uint8_t* compressed = (uint8_t*)MALLOC_PSRAM(content_length);
    if (!compressed) return false;
    _f.read(compressed, content_length);

    if (_cached_bin_data) {
        FREE_PSRAM(_cached_bin_data);
        _cached_bin_data = nullptr;
    }

    bool success = false;
    if (_compression == "zlib") {
        success = decompressZlib(compressed, content_length, _cached_bin_data, _cached_bin_size);
    } else if (_compression == "") {
        _cached_bin_data = compressed; 
        _cached_bin_size = content_length;
        compressed = nullptr; 
        success = true;
    } else {
        Serial.printf("Unsupported compression format: %s\n", _compression.c_str());
    }

    if (compressed) FREE_PSRAM(compressed);

    if (success) {
        _cached_bin_index = bin_index;
        _cached_bin_item_count = bin_item_count;
    }
    return success;
}

const uint8_t* Slob::getBlob(const BlobRef& ref, uint32_t& out_len, std::string& out_type) {
    if (!readStoreItem(ref.bin_index)) return nullptr;
    if (ref.item_index >= _cached_bin_item_count) return nullptr;

    uint32_t item_pos = getU32(_cached_bin_data + ref.item_index * 4);
    uint32_t data_start = 4 * _cached_bin_item_count + item_pos;

    if (data_start + 4 > _cached_bin_size) return nullptr;
    
    uint32_t content_len = getU32(_cached_bin_data + data_start);
    if (data_start + 4 + content_len > _cached_bin_size) return nullptr;

    out_len = content_len;

    uint8_t ct_id = _cached_content_types[ref.item_index];
    if (ct_id < _content_types.size()) {
        out_type = _content_types[ct_id];
    }

    return _cached_bin_data + data_start + 4;
}