#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ace/io.h>

struct io_buffer_fixture : ::testing::Test {};

// Verifies that expand() allocates writable storage.
TEST_F(io_buffer_fixture, buffer_expand) {
    ace::io::buffer buffer;
    EXPECT_NE(nullptr, buffer.expand(64));
}

// Verifies that repeated expand() calls contribute their full sizes to len().
TEST_F(io_buffer_fixture, buffer_expand_multiple) {
    ace::io::buffer buffer;
    buffer.expand(32);
    buffer.expand(64);
    buffer.expand(128);
    EXPECT_EQ(32u + 64u + 128u, buffer.len());
}

// Verifies that payload-plus-header overflow is rejected before allocation.
TEST_F(io_buffer_fixture, buffer_expand_overflow) {
    ace::io::buffer buffer;

    // A wrapped allocation would create a small chunk with a falsely enormous
    // iov_len, so both the exception and unchanged logical length matter.
    EXPECT_THROW(
        static_cast<void>(buffer.expand(std::numeric_limits<std::size_t>::max())),
        std::bad_alloc);
    EXPECT_EQ(0u, buffer.len());
}

// Verifies that string-view appends preserve order when converted to a string.
TEST_F(io_buffer_fixture, buffer_append_and_as_string) {
    ace::io::buffer buffer;
    buffer.append("hello");
    buffer.append(" world");
    EXPECT_EQ("hello world", buffer.as<std::string>());
}

// Verifies that clear() releases all chunks and resets the logical length.
TEST_F(io_buffer_fixture, buffer_clear) {
    ace::io::buffer buffer;
    buffer.append("test data");
    EXPECT_GT(buffer.len(), 0u);
    buffer.clear();
    EXPECT_EQ(0u, buffer.len());
}

// Verifies that clone() makes an independent full-data copy.
TEST_F(io_buffer_fixture, buffer_clone) {
    ace::io::buffer buffer;
    buffer.append("original");
    auto clone = buffer.clone();
    EXPECT_EQ("original", clone.as<std::string>());
    EXPECT_EQ(buffer.len(), clone.len());
}

// Verifies that move construction transfers all chunks and empties the source.
TEST_F(io_buffer_fixture, buffer_move) {
    ace::io::buffer source;
    source.append("data");
    const std::size_t original_length = source.len();
    ASSERT_GT(original_length, 0u);

    ace::io::buffer destination(std::move(source));
    EXPECT_EQ(original_length, destination.len());
    EXPECT_EQ(0u, source.len());
}

// Verifies that the std::formatter specialization emits buffer payload data.
TEST_F(io_buffer_fixture, buffer_formatter) {
    ace::io::buffer buffer;
    buffer.append("test");
    const auto formatted = std::format("{}", buffer);
    EXPECT_NE(std::string::npos, formatted.find("test"));
}

// Verifies that formatted append writes all substituted values.
TEST_F(io_buffer_fixture, buffer_append_format) {
    ace::io::buffer buffer;
    buffer.append("value={} id={}", 42, 7);
    EXPECT_EQ("value=42 id=7", buffer.as<std::string>());
}

// Verifies that append(first,last) copies the complete raw byte range.
TEST_F(io_buffer_fixture, buffer_append_raw) {
    int data[] = {10, 20, 30};
    ace::io::buffer buffer;
    buffer.append(static_cast<void*>(data), static_cast<void*>(data + 3));
    EXPECT_EQ(sizeof(data), buffer.len());
    EXPECT_EQ(sizeof(data), buffer.as<std::vector<std::byte>>().size());
}

// Verifies that appending a POD vector uses its byte size rather than element count.
TEST_F(io_buffer_fixture, buffer_append_vector) {
    const std::vector<int> values = {1, 2, 3, 4};
    ace::io::buffer buffer;
    buffer.append(values);
    EXPECT_EQ(values.size() * sizeof(int), buffer.len());
}

// Verifies that appending a fixed POD array copies every element byte.
TEST_F(io_buffer_fixture, buffer_append_array) {
    const std::array<int, 3> values = {5, 6, 7};
    ace::io::buffer buffer;
    buffer.append(values);
    EXPECT_EQ(values.size() * sizeof(int), buffer.len());
}

// Verifies byte-length calculation for a fixed-extent POD span.
TEST_F(io_buffer_fixture, buffer_append_span_fixed) {
    std::array<int, 3> values = {8, 9, 10};
    std::span<int, 3> span(values);
    ace::io::buffer buffer;
    buffer.append(span);
    EXPECT_EQ(3 * sizeof(int), buffer.len());
}

// Verifies runtime byte-length calculation for a dynamic-extent POD span.
TEST_F(io_buffer_fixture, buffer_append_span_dynamic) {
    std::vector<int> values = {11, 12};
    std::span<int> span(values);
    ace::io::buffer buffer;
    EXPECT_TRUE(buffer.append(span));
    EXPECT_EQ(values.size() * sizeof(int), buffer.len());
}

// Verifies that formatted prepend inserts a chunk before existing payload.
TEST_F(io_buffer_fixture, buffer_prepend_format) {
    ace::io::buffer buffer;
    buffer.append("world");
    buffer.prepend("hello ");
    EXPECT_EQ("hello world", buffer.as<std::string>());
}

// Verifies that string-view prepend preserves the expected payload order.
TEST_F(io_buffer_fixture, buffer_prepend_string_view) {
    ace::io::buffer buffer;
    buffer.append("bar");
    buffer.prepend(std::string_view("foo"));
    EXPECT_EQ("foobar", buffer.as<std::string>());
}

// Verifies that raw bytes can be prepended without changing the trailing payload.
TEST_F(io_buffer_fixture, buffer_prepend_raw) {
    const int header = 0xDEAD;
    ace::io::buffer buffer;
    buffer.append("data");
    const auto* first = &header;
    const auto* last = reinterpret_cast<const char*>(&header) + sizeof(header);
    buffer.prepend(first, last);
    EXPECT_EQ(sizeof(header) + 4, buffer.len());
}

// Verifies that each appendln() call adds exactly one trailing newline.
TEST_F(io_buffer_fixture, buffer_appendln) {
    ace::io::buffer buffer;
    buffer.appendln("first");
    buffer.appendln("second");
    EXPECT_EQ("first\nsecond\n", buffer.as<std::string>());
}

// Verifies that assemble() exposes one iovec per appended chunk.
TEST_F(io_buffer_fixture, buffer_assemble) {
    ace::io::buffer buffer;
    buffer.append("chunk1");
    buffer.append("chunk2");
    msghdr* header = buffer.assemble();
    ASSERT_NE(nullptr, header);
    EXPECT_EQ(2u, header->msg_iovlen);
    EXPECT_NE(nullptr, header->msg_iov);
}

// Verifies that repeated assemble() calls reuse the same message header metadata.
TEST_F(io_buffer_fixture, buffer_assemble_once) {
    ace::io::buffer buffer;
    buffer.append("data");
    msghdr* first = buffer.assemble();
    msghdr* second = buffer.assemble();

    // Pointer identity confirms the effect-once guard did not allocate a
    // second iovec array that would be leaked or freed independently.
    EXPECT_EQ(first, second);
    EXPECT_EQ(first->msg_iovlen, second->msg_iovlen);
}

// Verifies that disassemble() permits rebuilding metadata for the same chunks.
TEST_F(io_buffer_fixture, buffer_disassemble) {
    ace::io::buffer buffer;
    buffer.append("data");
    buffer.assemble();
    buffer.disassemble();
    msghdr* header = buffer.assemble();
    ASSERT_NE(nullptr, header);
    EXPECT_EQ(1u, header->msg_iovlen);
}

// Verifies that shape() reduces a tail reservation to the actual byte count.
TEST_F(io_buffer_fixture, buffer_shape) {
    ace::io::buffer buffer;
    buffer.expand(100);
    EXPECT_EQ(100u, buffer.len());
    buffer.shape(30);
    EXPECT_EQ(30u, buffer.len());
}

// Verifies that shaping a sole chunk preserves its retained prefix data.
TEST_F(io_buffer_fixture, buffer_shape_single) {
    ace::io::buffer buffer;
    buffer.append("hello world");
    buffer.shape(5);
    EXPECT_EQ(5u, buffer.len());
    EXPECT_EQ("hello", buffer.as<std::string>());
}

// Verifies that binary conversion returns the exact payload bytes in order.
TEST_F(io_buffer_fixture, buffer_as_bytes) {
    ace::io::buffer buffer;
    buffer.append("ab");
    const auto bytes = buffer.as<std::vector<std::byte>>();
    ASSERT_EQ(2u, bytes.size());
    EXPECT_EQ(std::byte('a'), bytes[0]);
    EXPECT_EQ(std::byte('b'), bytes[1]);
}

// Verifies that move assignment transfers chunks and empties the source buffer.
TEST_F(io_buffer_fixture, buffer_move_assign) {
    ace::io::buffer source;
    source.append("movable");
    const std::size_t original_length = source.len();
    ace::io::buffer destination;
    destination = std::move(source);
    EXPECT_EQ(original_length, destination.len());
    EXPECT_EQ(0u, source.len());
}
