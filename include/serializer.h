#pragma once
#include <vector>

namespace raft {

enum ByteOrder {
    BigEndian,
    LittleEndian,
};

enum DataType {
    Bool=0, Char, Int32, Int64, Float, Double,
    String, Structure, Unknown,
};


class Serializer {
private:
    char *buf_;
    int size_;
    int capacity_;
    int decode_ptr_;
    ByteOrder byte_order_;
public:
    Serializer();
    ~Serializer();

    // 写入数据类型DataType::Structure
    // 此方法禁止单独使用
    void EncodeStructureType() {
        DataType value_type = DataType::Structure;
        _write((char*)&value_type, sizeof(char));
    }

    // 读取数据类型DataType::Structure
    // 此方法禁止单独使用
    bool DecodeStructureType() {
        if (!_enableDecode()) {
            return false;
        }
        if (buf_[decode_ptr_] != DataType::Structure) {
            return false;
        }
        ++ decode_ptr_;
        return true;
    }

    template <typename E>
    void Encode(E value) {
        DataType value_type = _getDataType<E>();
        _write((char*)&value_type, sizeof(char)); // 写入数据类型

        int value_size = sizeof(E);
        if (value_size > 2 && byte_order_ == BigEndian) {
            char * first = (char*)&value;
            char * last = first + value_size;
            std::reverse(first, last);
        }

        _write((char*)&value, value_size);
    }

    template <typename E>
    bool Decode(E &value) {
        if (!_enableDecode()) {
            return false;
        }
        DataType value_type = _getDataType<E>();
        if (buf_[decode_ptr_] != value_type) {
            return false;
        }
        int value_size = sizeof(E);
        ++ decode_ptr_;
        value = *((E*)(&buf_[decode_ptr_]));
        decode_ptr_ += value_size;
        return true;
    }

    template <typename T>
    void EncodeObj(const T &obj) {
        obj.Encode(*this);
    }

    template <typename T>
    bool DecodeObj(T& obj) {
        if (!_enableDecode()) {
            return false;
        }
        int old_decode_ptr = decode_ptr_;
        bool result = obj.Decode(*this);
        if (!result) {
            decode_ptr_ = old_decode_ptr;
        }
        return result;
    }

    const char* Bytes() { return buf_; }


private:
    void _reserve(int len);
    void _write(const char *data, int len);
    template <typename E>
    DataType _getDataType();
    bool _enableDecode() const { // 不能再Decode了
        if (decode_ptr_ == size_) {
            return false;
        }
        return true;
    }
};


template <typename E>
DataType Serializer::_getDataType() {
    if (std::is_same<E, bool>::value) {
        return DataType::Bool;
    } else if (std::is_same<E, char>::value) {
        return DataType::Char;
    } else if (std::is_same<E, int32_t>::value) {
        return DataType::Int32;
    } else if (std::is_same<E, int64_t>::value) {
        return DataType::Int64;
    } else if (std::is_same<E, float>::value) {
        return DataType::Float;
    } else if (std::is_same<E, double>::value) {
        return DataType::Double;
    } else if (std::is_same<E, std::string>::value) {
        return DataType::String;
    } else {
        return DataType::Unknown;
    }
}


}