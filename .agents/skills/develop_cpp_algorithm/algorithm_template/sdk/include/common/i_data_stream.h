#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <common/data_types.h>
#include <common/span.h>

namespace cy::common {

enum class DataType {
    Int16,
    Int32,
    Float32,
    UInt8,
    CS16,     // 16位有符号复数 (雷达常见 IQ)
    CF32,     // 32位单精度复数 (雷达常见 IQ)
    RawBytes  // 原始无类型二进制流
};

inline std::string_view DataTypeToString(DataType data_type) {
    switch (data_type) {
    case DataType::Int16:
        return "Int16";
    case DataType::Int32:
        return "Int32";
    case DataType::Float32:
        return "Float32";
    case DataType::UInt8:
        return "UInt8";
    case DataType::CS16:
        return "CS16";
    case DataType::CF32:
        return "CF32";
    case DataType::RawBytes:
        return "RawBytes";
    }
    return "Unknown";
}

inline std::size_t DataTypeElementSize(DataType data_type) {
    switch (data_type) {
    case DataType::Int16:
        return sizeof(std::int16_t);
    case DataType::Int32:
        return sizeof(std::int32_t);
    case DataType::Float32:
        return sizeof(float);
    case DataType::UInt8:
        return sizeof(std::uint8_t);
    case DataType::CS16:
        return sizeof(CS16);
    case DataType::CF32:
        return sizeof(CF32);
    case DataType::RawBytes:
        return 1;
    }
    return 0;
}

inline bool DataTypeTokenEquals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        char lch = lhs[i];
        char rch = rhs[i];
        if (lch >= 'A' && lch <= 'Z') {
            lch = static_cast<char>(lch - 'A' + 'a');
        }
        if (rch >= 'A' && rch <= 'Z') {
            rch = static_cast<char>(rch - 'A' + 'a');
        }
        if (lch != rch) {
            return false;
        }
    }
    return true;
}

inline bool TryParseDataType(std::string_view value, DataType& out) {
    if (value.empty()) {
        return false;
    }
    if (DataTypeTokenEquals(value, "int16")) {
        out = DataType::Int16;
        return true;
    }
    if (DataTypeTokenEquals(value, "int32")) {
        out = DataType::Int32;
        return true;
    }
    if (DataTypeTokenEquals(value, "float") || DataTypeTokenEquals(value, "float32")) {
        out = DataType::Float32;
        return true;
    }
    if (DataTypeTokenEquals(value, "uint8")) {
        out = DataType::UInt8;
        return true;
    }
    if (DataTypeTokenEquals(value, "cs16")) {
        out = DataType::CS16;
        return true;
    }
    if (DataTypeTokenEquals(value, "cf32")) {
        out = DataType::CF32;
        return true;
    }
    if (DataTypeTokenEquals(value, "raw") ||
        DataTypeTokenEquals(value, "rawbytes") ||
        DataTypeTokenEquals(value, "bytes")) {
        out = DataType::RawBytes;
        return true;
    }
    return false;
}

class IDataReader {
public:
    virtual ~IDataReader() = default;
    virtual bool is_active() const = 0;

    virtual DataType get_data_type() const = 0;
    virtual std::size_t get_element_size() const = 0; // 每个数据元素的物理字节大小

    /// 💡 读接口：仅接受字节缓冲区视图
    /// @param buffer 可写的字节缓冲区视图 (std::byte 类型精准表达原始字节)
    /// @return 实际成功读取的字节数，<=0 表示超时、错误或结束
    virtual int read(Span<std::byte> buffer, long timeout_us = 100000) = 0;
};

class IDataWriter {
public:
    virtual ~IDataWriter() = default;
    virtual bool is_active() const = 0;

    virtual DataType get_data_type() const = 0;
    virtual std::size_t get_element_size() const = 0;

    /// 💡 写接口：仅接受只读字节缓冲区视图
    /// @return 实际成功写入的字节数，<=0 表示出错
    virtual int write(Span<const std::byte> buffer, long timeout_us = 100000) = 0;
};

} // namespace cy::common
