#include "serializer.h"

namespace raft {


Serializer::Serializer()
    : buf_(nullptr),
      size_(0),
      capacity_(0),
      decode_ptr_(0) {
    byte_order_ = __BYTE_ORDER__ == BIG_ENDIAN ? BigEndian : LittleEndian;
}



Serializer::Serializer(int init_size) {
    byte_order_ = __BYTE_ORDER__ == BIG_ENDIAN ? BigEndian : LittleEndian;
    buf_ = new char[init_size];
    size_ = 0;
    capacity_ = init_size;
    decode_ptr_ = 0;
}


Serializer::Serializer(const char *data, int len) {
    byte_order_ = __BYTE_ORDER__ == BIG_ENDIAN ? BigEndian : LittleEndian;
    buf_ = new char[len];
    std::memcpy(buf_, data, len);
    size_ = len;
    capacity_ = len;
    decode_ptr_ = 0;
}

Serializer::~Serializer() {
    delete [] buf_;
    buf_ = nullptr;
}


void Serializer::_write(const char *data, int len) {
    _reserve(len);
    std::memcpy(buf_+size_, data, len);
    size_ += len;
}


void Serializer::_reserve(int len) {
    if (size_ + len > capacity_) {
        while (size_ + len > capacity_) {
            if (capacity_ == 0) {
                capacity_ = 1;
            } else {
                capacity_ *= 2;
            }
        }
        char * new_buf = new char[capacity_];
        std::memcpy(new_buf, buf_, size_);
        if (buf_ != nullptr && size_ > 0) {
            delete []buf_;
        }
        buf_ = new_buf;
    }
}

}