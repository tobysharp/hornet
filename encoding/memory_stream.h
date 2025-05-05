#include <streambuf>
#include <iostream>
#include <span>
#include <vector>

namespace detail {

template <typename T = uint8_t>
class VectorStreamBuf : public std::streambuf {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
 public:
    explicit VectorStreamBuf(std::vector<T>& vec) : buffer_(vec) {}

 protected:
    // Called when ostream wants to write a character
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            buffer_.push_back(static_cast<T>(ch));
            return ch;
        }
        return traits_type::eof();
    }

    // Write multiple characters
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        static_assert(sizeof(T) == 1);  // T must be one byte for this current casting implementation
        buffer_.insert(buffer_.end(), reinterpret_cast<const T*>(s), reinterpret_cast<const T*>(s + n));
        return n;
    }

private:
    std::vector<T>& buffer_;
};

template <typename T = uint8_t>
class SpanStreamBuf : public std::streambuf {
 public:
    explicit SpanStreamBuf(std::span<const T> span) {
        setg(   const_cast<char*>(reinterpret_cast<const char*>(span.data())),
                const_cast<char*>(reinterpret_cast<const char*>(span.data())), 
                const_cast<char*>(reinterpret_cast<const char*>(span.data()) + span.size()));
    }
};

}

class MemoryOStream : public std::ostream {
public:
    MemoryOStream() : MemoryOStream(alloc_) {}
    explicit MemoryOStream(std::vector<uint8_t>& vec) : std::ostream(&buf_), write_(vec), buf_(write_) {}
    const std::vector<uint8_t>& Buffer() const { return write_; }

private:
    std::vector<uint8_t> alloc_;
    std::vector<uint8_t>& write_;
    detail::VectorStreamBuf<uint8_t> buf_;
};

class MemoryIStream : public std::istream {
public:
    explicit MemoryIStream(std::span<const uint8_t> data)
        : std::istream(&buf), buf(data) {}
private:
    detail::SpanStreamBuf<uint8_t> buf;
};
