#include "RingBuffer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace miab::ring_buffer {

namespace {

constexpr std::size_t PREALLOCATE_CHUNK_SIZE = 256;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

RingBuffer::RingBuffer(
    fs::FS& filesystem,
    std::string path,
    std::uint32_t maxEntries)
    : _filesystem(filesystem),
      _path(std::move(path)),
      _requestedMaxEntries(maxEntries)
{
    if (_requestedMaxEntries == 0) {
        throw std::invalid_argument("Ring-buffer capacity must be greater than zero");
    }
}

RingBuffer::~RingBuffer()
{
    close();
}

void RingBuffer::open()
{
    close();

    if (_filesystem.exists(_path.c_str())) {
        openExistingFile();
    } else {
        createNewFile();
    }

    reconstructState();
}

void RingBuffer::close()
{
    if (_file) {
        _file.close();
    }
}

bool RingBuffer::isOpen() const
{
    return static_cast<bool>(_file);
}

void RingBuffer::createNewFile()
{
    _header = {};
    std::memcpy(_header.magic, MAGIC, sizeof(MAGIC));
    _header.version = FORMAT_VERSION;
    _header.header_size = HEADER_SIZE;
    _header.entry_size = ENTRY_SIZE;
    _header.max_entries = _requestedMaxEntries;
    _header.crc32 = calculateHeaderCrc(_header);

    _file = _filesystem.open(_path.c_str(), "w+");
    require(static_cast<bool>(_file), "Failed to create ring-buffer file");

    require(
        _file.write(reinterpret_cast<const std::uint8_t*>(&_header), HEADER_SIZE) == HEADER_SIZE,
        "Failed to write ring-buffer header");

    // Preallocate every slot as zero bytes. An empty slot has id == 0 and is
    // intentionally not a valid record.
    const std::size_t dataBytes =
        static_cast<std::size_t>(_header.max_entries) * ENTRY_SIZE;

    const std::array<std::uint8_t, PREALLOCATE_CHUNK_SIZE> zeros{};
    std::size_t remaining = dataBytes;

    while (remaining > 0) {
        const std::size_t chunk = std::min(remaining, zeros.size());
        require(
            _file.write(zeros.data(), chunk) == chunk,
            "Failed to preallocate ring-buffer records");
        remaining -= chunk;
    }

    _file.flush();
    require(
        _file.size() == HEADER_SIZE + dataBytes,
        "Ring-buffer preallocation produced an unexpected file size");
}

void RingBuffer::openExistingFile()
{
    _file = _filesystem.open(_path.c_str(), "r+");
    require(static_cast<bool>(_file), "Failed to open ring-buffer file");

    require(_file.seek(0, SeekSet), "Failed to seek to ring-buffer header");
    require(
        _file.read(reinterpret_cast<std::uint8_t*>(&_header), HEADER_SIZE) == HEADER_SIZE,
        "Failed to read ring-buffer header");

    if (!isValidHeader(_header)) {
        throw std::runtime_error(
            "Unsupported or corrupt ring-buffer file. This implementation expects format v2");
    }

    const std::size_t expectedSize =
        HEADER_SIZE + static_cast<std::size_t>(_header.max_entries) * ENTRY_SIZE;

    require(
        _file.size() == expectedSize,
        "Ring-buffer file size does not match its header");
}

void RingBuffer::reconstructState()
{
    auto entries = scanValidEntries();

    _count = static_cast<std::uint32_t>(entries.size());
    _nextId = entries.empty() ? 1 : entries.back().id + 1;

    if (_nextId == 0) {
        throw std::overflow_error("Ring-buffer message ID space exhausted");
    }
}

Entry RingBuffer::writeNext(
    const std::string& handle,
    const std::string& message)
{
    require(isOpen(), "Ring buffer is not open");

    // std::string::size() is the UTF-8 byte count for the API strings, which is
    // exactly what the fixed on-disk fields limit.
    if (handle.empty() || handle.size() >= HANDLE_MAX_SIZE) {
        throw std::range_error("Handle must contain 1-31 UTF-8 bytes");
    }

    if (message.empty() || message.size() >= MESSAGE_MAX_SIZE) {
        throw std::range_error("Message must contain 1-127 UTF-8 bytes");
    }

    Entry entry{};
    entry.id = _nextId;
    std::memcpy(entry.handle, handle.data(), handle.size());
    std::memcpy(entry.message, message.data(), message.size());
    entry.crc32 = calculateEntryCrc(entry);

    const std::uint32_t slot = static_cast<std::uint32_t>(
        (entry.id - 1) % _header.max_entries);

    writeSlot(slot, entry);

    // Verify the persisted bytes before exposing the ID to the HTTP layer.
    Entry verified{};
    if (!readSlot(slot, verified) || verified.id != entry.id) {
        throw std::runtime_error("Ring-buffer write verification failed");
    }

    ++_nextId;
    if (_count < _header.max_entries) {
        ++_count;
    }

    return entry;
}

std::vector<Entry> RingBuffer::readAll()
{
    require(isOpen(), "Ring buffer is not open");
    return scanValidEntries();
}

std::vector<Entry> RingBuffer::readAfter(MessageId afterId)
{
    require(isOpen(), "Ring buffer is not open");

    std::vector<Entry> entries;

    if (_count == 0) {
        return entries;
    }

    const MessageId latest = _nextId - 1;

    if (afterId >= latest) {
        return entries;
    }

    // Under normal operation, the retained IDs form one contiguous range.
    const MessageId oldest =
        latest - static_cast<MessageId>(_count) + 1;

    // afterId + 1 cannot overflow here because afterId < latest.
    const MessageId first =
        afterId < oldest ? oldest : afterId + 1;

    const std::size_t resultCount =
        static_cast<std::size_t>(latest - first + 1);

    entries.reserve(resultCount);

    for (MessageId id = first;; ++id) {
        const std::uint32_t slot =
            static_cast<std::uint32_t>(
                (id - 1) % _header.max_entries
            );

        Entry entry{};

        if (readSlot(slot, entry) && entry.id == id) {
            entries.push_back(entry);
        }

        // Avoid relying on id <= latest, which could wrap at UINT64_MAX.
        if (id == latest) {
            break;
        }
    }

    return entries;
}

MessageId RingBuffer::latestId() const noexcept
{
    return _nextId - 1;
}

std::uint32_t RingBuffer::count() const noexcept
{
    return _count;
}

std::uint32_t RingBuffer::capacity() const noexcept
{
    return _header.max_entries;
}

std::vector<Entry> RingBuffer::scanValidEntries()
{
    std::vector<Entry> entries;
    entries.reserve(_header.max_entries);

    for (std::uint32_t slot = 0; slot < _header.max_entries; ++slot) {
        Entry entry{};
        if (readSlot(slot, entry)) {
            entries.push_back(entry);
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const Entry& left, const Entry& right) {
            return left.id < right.id;
        });

    // IDs should be unique because each ID maps to exactly one ring slot. If a
    // damaged or externally modified file contains duplicates, retain only one.
    entries.erase(
        std::unique(
            entries.begin(),
            entries.end(),
            [](const Entry& left, const Entry& right) {
                return left.id == right.id;
            }),
        entries.end());

    if (entries.size() > _header.max_entries) {
        entries.erase(
            entries.begin(),
            entries.end() - static_cast<std::ptrdiff_t>(_header.max_entries));
    }

    return entries;
}

bool RingBuffer::readSlot(std::uint32_t slot, Entry& entry)
{
    if (slot >= _header.max_entries) {
        return false;
    }

    if (!_file.seek(slotOffset(slot), SeekSet)) {
        throw std::runtime_error("Failed to seek while reading ring-buffer slot");
    }

    if (_file.read(reinterpret_cast<std::uint8_t*>(&entry), ENTRY_SIZE) != ENTRY_SIZE) {
        throw std::runtime_error("Failed to read complete ring-buffer slot");
    }

    return isValidEntry(entry);
}

void RingBuffer::writeSlot(std::uint32_t slot, const Entry& entry)
{
    if (slot >= _header.max_entries) {
        throw std::out_of_range("Ring-buffer slot out of range");
    }

    require(
        _file.seek(slotOffset(slot), SeekSet),
        "Failed to seek while writing ring-buffer slot");

    require(
        _file.write(reinterpret_cast<const std::uint8_t*>(&entry), ENTRY_SIZE) == ENTRY_SIZE,
        "Failed to write complete ring-buffer slot");

    _file.flush();
}

std::size_t RingBuffer::slotOffset(std::uint32_t slot) const
{
    return HEADER_SIZE + static_cast<std::size_t>(slot) * ENTRY_SIZE;
}

bool RingBuffer::isValidHeader(const Header& header) const
{
    return std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) == 0 &&
           header.version == FORMAT_VERSION &&
           header.header_size == HEADER_SIZE &&
           header.entry_size == ENTRY_SIZE &&
           header.max_entries > 0 &&
           header.crc32 == calculateHeaderCrc(header);
}

bool RingBuffer::isValidEntry(const Entry& entry) const
{
    if (entry.id == 0 || entry.crc32 != calculateEntryCrc(entry)) {
        return false;
    }

    // A valid record must contain a terminator inside each fixed string field.
    return std::memchr(entry.handle, '\0', HANDLE_MAX_SIZE) != nullptr &&
           std::memchr(entry.message, '\0', MESSAGE_MAX_SIZE) != nullptr;
}

std::uint32_t RingBuffer::calculateCrc32(const void* data, std::size_t length)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFFu;

    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1u)));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

std::uint32_t RingBuffer::calculateHeaderCrc(const Header& header)
{
    return calculateCrc32(&header, offsetof(Header, crc32));
}

std::uint32_t RingBuffer::calculateEntryCrc(const Entry& entry)
{
    return calculateCrc32(&entry, offsetof(Entry, crc32));
}

} // namespace miab::ring_buffer
