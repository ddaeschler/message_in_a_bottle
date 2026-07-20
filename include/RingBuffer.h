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

/**
 * Static on-disk metadata.
 *
 * This header is written only when the file is created. Runtime state such as
 * the next ID and retained count is reconstructed by scanning the records at
 * startup, avoiding a hot metadata write for every posted message.
 */
struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t header_size;
    std::uint32_t entry_size;
    std::uint32_t max_entries;
    std::uint32_t crc32;
};

/**
 * Fixed-size persistent guestbook message.
 *
 * `id` is the API-visible monotonically increasing message ID. A CRC makes an
 * interrupted or partial flash write distinguishable from a valid message.
 */
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

/**
 * Persistent fixed-record guestbook ring buffer backed by an Arduino FS.
 *
 * Typical construction:
 *
 *     RingBuffer guestbook{LittleFS, "/ring_buffer.bin"};
 *     guestbook.open();
 *
 * Records are returned oldest-to-newest, which matches the chat UI. The
 * `readAfter()` method directly supports GET /read?afterId=<id>.
 */
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
    void open();

    /** Close the underlying file handle. */
    void close();

    [[nodiscard]] bool isOpen() const;

    /**
     * Persist one message and return the complete stored record, including its
     * newly assigned API-visible ID.
     */
    Entry writeNext(const std::string& handle, const std::string& message);

    /** Return all currently retained messages, oldest-to-newest. */
    [[nodiscard]] std::vector<Entry> readAll();

    /** Return retained messages with id > afterId, oldest-to-newest. */
    [[nodiscard]] std::vector<Entry> readAfter(MessageId afterId);

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

    void createNewFile();
    void openExistingFile();
    void reconstructState();

    [[nodiscard]] std::vector<Entry> scanValidEntries();
    [[nodiscard]] bool readSlot(std::uint32_t slot, Entry& entry);
    void writeSlot(std::uint32_t slot, const Entry& entry);

    [[nodiscard]] std::size_t slotOffset(std::uint32_t slot) const;
    [[nodiscard]] bool isValidHeader(const Header& header) const;
    [[nodiscard]] bool isValidEntry(const Entry& entry) const;

    static std::uint32_t calculateCrc32(const void* data, std::size_t length);
    static std::uint32_t calculateHeaderCrc(const Header& header);
    static std::uint32_t calculateEntryCrc(const Entry& entry);
};

} // namespace miab::ring_buffer
