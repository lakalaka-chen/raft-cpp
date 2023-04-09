#include "gtest/gtest.h"
#include "serializer.h"
#include <string>


class SerializerTest: public testing::Test {
public:
    SerializerTest() = default;
    raft::Serializer serializer_;
};


TEST_F(SerializerTest, BoolTest) {
    serializer_.Encode(true);
    bool decode_value;
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, true);
    serializer_.Encode(false);
    serializer_.Encode(false);
    serializer_.Encode(true);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, false);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, false);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, true);
}


TEST_F(SerializerTest, CharTest) {
    serializer_.Encode('a');
    char decode_value;
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, 'a');
    serializer_.Encode('b');
    serializer_.Encode('c');
    serializer_.Encode('z');
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, 'b');
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, 'c');
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, 'z');
}

TEST_F(SerializerTest, Int32Test) {
    int32_t encode_value = 12;
    serializer_.Encode(encode_value);
    int32_t decode_value;
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value);
    serializer_.Encode(encode_value+1);
    serializer_.Encode(encode_value+2);
    serializer_.Encode(encode_value+3);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+1);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+2);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+3);
}


TEST_F(SerializerTest, Int64Test) {
    int64_t encode_value = 12;
    serializer_.Encode(encode_value);
    int64_t decode_value;
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value);
    serializer_.Encode(encode_value+1);
    serializer_.Encode(encode_value+2);
    serializer_.Encode(encode_value+3);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+1);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+2);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+3);
}

TEST_F(SerializerTest, FloatTest) {
    float encode_value = 12.12;
    serializer_.Encode(encode_value);
    float decode_value;
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value);
    serializer_.Encode(encode_value+1);
    serializer_.Encode(encode_value+2);
    serializer_.Encode(encode_value+3);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+1);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+2);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+3);
}


TEST_F(SerializerTest, DoubleTest) {
    double encode_value = 12.12345;
    serializer_.Encode(encode_value);
    double decode_value;
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value);
    serializer_.Encode(encode_value+1);
    serializer_.Encode(encode_value+2);
    serializer_.Encode(encode_value+3);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+1);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+2);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+3);
}


TEST_F(SerializerTest, StringTest) {
    std::string encode_value = "hello";
    serializer_.Encode(encode_value);
    std::string decode_value;
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value);
    serializer_.Encode(encode_value+encode_value);
    serializer_.Decode(decode_value);
    ASSERT_EQ(decode_value, encode_value+encode_value);
}


TEST_F(SerializerTest, MultiTypeTest) {
    serializer_.Encode(12);
    serializer_.Encode(12.12);
    serializer_.Encode('x');
    serializer_.Encode(false);
    serializer_.Encode("hello");
    serializer_.Encode("nihao");
    int int_value;
    serializer_.Decode(int_value);
    ASSERT_EQ(int_value, 12);
    double double_value;
    serializer_.Decode(double_value);
    ASSERT_EQ(double_value, 12.12);
}


TEST_F(SerializerTest, DecodeFailTest) {
    serializer_.Encode(12);
    bool value;
    ASSERT_EQ(false, serializer_.Decode(value));
    int int_value;
    ASSERT_EQ(true, serializer_.Decode(int_value));
    ASSERT_EQ(12, int_value);
}


struct Object {
    int term_{2};
    int index_{2};

    void Encode(raft::Serializer &serializer) const {
        serializer.EncodeStructureType();
        serializer.Encode(term_);
        serializer.Encode(index_);
    }

    bool Decode(raft::Serializer &serializer) {
        if (!serializer.DecodeStructureType()) return false;
        if (!serializer.Decode(term_)) return false;
        if (!serializer.Decode(index_)) return false;
        return true;
    }

    bool operator==(const Object &o) const {
        return o.term_ == term_ && o.index_ == index_;
    }
};

struct AnotherObject {
    int term_{0};
    int index_{0};
    double time_{0.02};
    void Encode(raft::Serializer &serializer) const {
        serializer.EncodeStructureType();
        serializer.Encode(term_);
        serializer.Encode(index_);
        serializer.Encode(time_);
    }
    bool Decode(raft::Serializer &serializer) {
        if (!serializer.DecodeStructureType()) return false;
        if (!serializer.Decode(term_)) return false;
        if (!serializer.Decode(index_)) return false;
        if (!serializer.Decode(time_)) return false;
        return true;
    }
    bool operator==(const AnotherObject &o) const {
        return o.term_ == term_ && o.index_ == index_ && o.time_ == time_;
    }
};

TEST(ObjectEncodeTest, TEST1) {
    raft::Serializer serializer;
    Object obj{3,5};
    serializer.EncodeObj(obj);
    Object obj2;
    ASSERT_EQ(true, serializer.DecodeObj(obj2));
    ASSERT_EQ(obj2, obj);

    serializer.EncodeObj(obj);
    serializer.Encode(16);

    AnotherObject ano_obj;

    ASSERT_EQ(false, serializer.DecodeObj(ano_obj));
    ASSERT_EQ(true, serializer.DecodeObj(obj2));
    int decoded_int;
    ASSERT_EQ(true, serializer.Decode(decoded_int));
    ASSERT_EQ(16, decoded_int);
}