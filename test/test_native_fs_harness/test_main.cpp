#include <unity.h>

#include "LittleFS.h"
#include "core/Esp32BaseLog.h"
#include "runtime/Esp32BaseFs.h"
#include "runtime/internal/Esp32BaseFsInternal.h"

#include <cstring>
#include <cstdio>
#include <vector>

void Esp32BaseLog::formatBytes(uint64_t bytes, char* out, size_t length) {
    if (out && length > 0) {
        std::snprintf(out, length, "%llu", static_cast<unsigned long long>(bytes));
    }
}

#include "../../src/runtime/Esp32BaseFs.inc"

namespace {

std::vector<uint8_t> readAll(const char* path) {
    const int64_t size = Esp32BaseFs::fileSize(path);
    if (size < 0) {
        return {};
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    size_t readLength = 0;
    if (!data.empty() && !Esp32BaseFs::readBytes(path, data.data(), data.size(), &readLength)) {
        return {};
    }
    data.resize(readLength);
    return data;
}

void assertBytes(const char* path, const uint8_t* expected, size_t length) {
    const std::vector<uint8_t> actual = readAll(path);
    TEST_ASSERT_EQUAL_UINT32(length, actual.size());
    if (length > 0) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual.data(), length);
    }
}

}  // namespace

void setUp() {
    native_littlefs::reset();
    TEST_ASSERT_TRUE(Esp32BaseFs::begin());
}

void tearDown() {}

void test_write_bytes_creates_missing_file_and_preserves_overwrite_behavior() {
    TEST_ASSERT_TRUE(Esp32BaseFs::writeBytes("/empty-write.bin", nullptr, 0));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists("/empty-write.bin"));
    TEST_ASSERT_EQUAL_INT64(0, Esp32BaseFs::fileSize("/empty-write.bin"));

    std::vector<uint8_t> initial(1300);
    for (size_t i = 0; i < initial.size(); ++i) {
        initial[i] = static_cast<uint8_t>(i & 0xFFU);
    }
    TEST_ASSERT_FALSE(Esp32BaseFs::exists("/write.bin"));
    TEST_ASSERT_TRUE(Esp32BaseFs::writeBytes("/write.bin", initial.data(), initial.size()));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists("/write.bin"));
    assertBytes("/write.bin", initial.data(), initial.size());

    const uint8_t replacement[] = {9, 8, 7};
    TEST_ASSERT_TRUE(Esp32BaseFs::writeBytes("/write.bin", replacement, sizeof(replacement)));
    assertBytes("/write.bin", replacement, sizeof(replacement));
}

void test_append_bytes_creates_missing_file_and_preserves_append_behavior() {
    TEST_ASSERT_TRUE(Esp32BaseFs::appendBytes("/empty-append.bin", nullptr, 0));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists("/empty-append.bin"));
    TEST_ASSERT_EQUAL_INT64(0, Esp32BaseFs::fileSize("/empty-append.bin"));

    const uint8_t first[] = {1, 2, 3};
    const uint8_t second[] = {4, 5};
    const uint8_t expected[] = {1, 2, 3, 4, 5};

    TEST_ASSERT_FALSE(Esp32BaseFs::exists("/append.bin"));
    TEST_ASSERT_TRUE(Esp32BaseFs::appendBytes("/append.bin", first, sizeof(first)));
    TEST_ASSERT_TRUE(Esp32BaseFs::appendBytes("/append.bin", second, sizeof(second)));
    assertBytes("/append.bin", expected, sizeof(expected));
}

void test_create_fixed_file_creates_missing_file_and_preserves_usable_content() {
    TEST_ASSERT_TRUE(Esp32BaseFs::createFixedFile("/empty-fixed.bin", 0, 0xA5));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists("/empty-fixed.bin"));
    TEST_ASSERT_EQUAL_INT64(0, Esp32BaseFs::fileSize("/empty-fixed.bin"));

    TEST_ASSERT_FALSE(Esp32BaseFs::exists("/fixed.bin"));
    TEST_ASSERT_TRUE(Esp32BaseFs::createFixedFile("/fixed.bin", 1300, 0xA5));

    const std::vector<uint8_t> created = readAll("/fixed.bin");
    TEST_ASSERT_EQUAL_UINT32(1300, created.size());
    for (const uint8_t value : created) {
        TEST_ASSERT_EQUAL_HEX8(0xA5, value);
    }

    const uint8_t replacement = 0x3C;
    TEST_ASSERT_TRUE(Esp32BaseFs::writeBytesAt("/fixed.bin", 650, &replacement, 1));
    TEST_ASSERT_TRUE(Esp32BaseFs::createFixedFile("/fixed.bin", 1300, 0xA5));
    const std::vector<uint8_t> preserved = readAll("/fixed.bin");
    TEST_ASSERT_EQUAL_HEX8(replacement, preserved[650]);
}

void test_internal_segment_create_and_first_append_create_missing_files() {
    const uint8_t first[] = {1, 2};
    const uint8_t second[] = {3, 4, 5};
    const esp32base_internal::FsWriteSegment segments[] = {
        {first, sizeof(first)},
        {second, sizeof(second)},
    };
    const uint8_t expected[] = {1, 2, 3, 4, 5};

    TEST_ASSERT_TRUE(esp32base_internal::fsCreateWithSegments("/segments.bin", segments, 2));
    assertBytes("/segments.bin", expected, sizeof(expected));

    TEST_ASSERT_TRUE(esp32base_internal::fsAppendSegments("/append-segments.bin", segments, 2));
    assertBytes("/append-segments.bin", expected, sizeof(expected));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_write_bytes_creates_missing_file_and_preserves_overwrite_behavior);
    RUN_TEST(test_append_bytes_creates_missing_file_and_preserves_append_behavior);
    RUN_TEST(test_create_fixed_file_creates_missing_file_and_preserves_usable_content);
    RUN_TEST(test_internal_segment_create_and_first_append_create_missing_files);
    return UNITY_END();
}
