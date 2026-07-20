#pragma once

#include <FS.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace miab::ring_buffer {

constexpr std::uint32_t HANDLE_MAX_SIZE = 32;
constexpr std::uint32_t MESSAGE_MAX_SIZE = 128;
constexpr std::uint32_t DEFAULT_MAX_ENTRIES = 1000;

using MessageId = std::uint64_t;

#pragma pack(push, 1)

struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t header_size;
    std::uint32_t entry_size;
    std::uint32_t max_entries;
    std::uint32_t crc32;
};

struct Entry {
    MessageId id;
    char handle[HANDLE_MAX_SIZE];
    char message[MESSAGE_MAX_SIZE];
    std::uint32_t crc32;
};

#pragma pack(pop)

static_assert(sizeof(Header) == 28, "Unexpected ring-buffer header layout");
static_assert(sizeof(Entry) == 172, "Unexpected ring-buffer entry layout");

constexpr std::uint32_t HEADER_SIZE = sizeof(Header);
constexpr std::uint32_t ENTRY_SIZE = sizeof(Entry);

enum class Error : std::uint8_t {
    none = 0,
    invalid_capacity,
    not_open,
    invalid_handle,
    invalid_message,
    file_create_failed,
    file_open_failed,
    file_seek_failed,
    file_read_failed,
    file_write_failed,
    invalid_header,
    invalid_file_size,
    preallocation_failed,
    write_verification_failed,
    id_exhausted,
    slot_out_of_range
};

[[nodiscard]] const char* errorMessage(Error error) noexcept;

struct WriteResult {
    Error error = Error::none;
    Entry entry{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Error::none;
    }
};

class RingBuffer {
public:
    RingBuffer(
        fs::FS& filesystem,
        std::string path = "/ring_buffer.bin",
        std::uint32_t maxEntries = DEFAULT_MAX_ENTRIES);

    ~RingBuffer();

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /** Open and validate an existing file, or create and preallocate a new one. */
    [[nodiscard]] Error open();

    /** Close the underlying file handle and clear reconstructed runtime state. */
    void close();

    [[nodiscard]] bool isOpen() const;

    /** Persist one message and return its complete stored record. */
    [[nodiscard]] WriteResult writeNext(
        const std::string& handle,
        const std::string& message);

    /** Replace `entries` with all retained records, oldest-to-newest. */
    [[nodiscard]] Error readAll(std::vector<Entry>& entries);

    /** Replace `entries` with retained records whose ID is greater than afterId. */
    [[nodiscard]] Error readAfter(
        MessageId afterId,
        std::vector<Entry>& entries);

    [[nodiscard]] MessageId latestId() const noexcept;
    [[nodiscard]] std::uint32_t count() const noexcept;
    [[nodiscard]] std::uint32_t capacity() const noexcept;

private:
    static constexpr char MAGIC[8] = {'M', 'I', 'A', 'B', 'R', 'B', '2', '\0'};
    static constexpr std::uint32_t FORMAT_VERSION = 2;

    fs::FS& _filesystem;
    std::string _path;
    File _file;
    Header _header{};
    std::uint32_t _requestedMaxEntries;
    MessageId _nextId = 1;
    std::uint32_t _count = 0;

    [[nodiscard]] Error createNewFile();
    [[nodiscard]] Error openExistingFile();
    [[nodiscard]] Error reconstructState();

    [[nodiscard]] Error scanValidEntries(std::vector<Entry>& entries);
    [[nodiscard]] Error readSlot(
        std::uint32_t slot,
        Entry& entry,
        bool& isValid);
    [[nodiscard]] Error writeSlot(std::uint32_t slot, const Entry& entry);

    [[nodiscard]] std::size_t slotOffset(std::uint32_t slot) const;
    [[nodiscard]] bool isValidHeader(const Header& header) const;
    [[nodiscard]] bool isValidEntry(const Entry& entry) const;

    static std::uint32_t calculateCrc32(const void* data, std::size_t length);
    static std::uint32_t calculateHeaderCrc(const Header& header);
    static std::uint32_t calculateEntryCrc(const Entry& entry);
};

} // namespace miab::ring_buffer
