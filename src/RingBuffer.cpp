#include "RingBuffer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace miab::ring_buffer {

namespace {

constexpr std::size_t PREALLOCATE_CHUNK_SIZE = 256;

} // namespace

const char* errorMessage(Error error) noexcept
{
    switch (error) {
    case Error::none: return "no error";
    case Error::invalid_capacity: return "ring-buffer capacity must be greater than zero";
    case Error::not_open: return "ring buffer is not open";
    case Error::invalid_handle: return "handle must contain 1-31 UTF-8 bytes";
    case Error::invalid_message: return "message must contain 1-127 UTF-8 bytes";
    case Error::file_create_failed: return "failed to create ring-buffer file";
    case Error::file_open_failed: return "failed to open ring-buffer file";
    case Error::file_seek_failed: return "failed to seek in ring-buffer file";
    case Error::file_read_failed: return "failed to read ring-buffer file";
    case Error::file_write_failed: return "failed to write ring-buffer file";
    case Error::invalid_header: return "unsupported or corrupt ring-buffer header";
    case Error::invalid_file_size: return "ring-buffer file size does not match its header";
    case Error::preallocation_failed: return "ring-buffer preallocation failed";
    case Error::write_verification_failed: return "ring-buffer write verification failed";
    case Error::id_exhausted: return "ring-buffer message ID space exhausted";
    case Error::slot_out_of_range: return "ring-buffer slot is out of range";
    }

    return "unknown ring-buffer error";
}

RingBuffer::RingBuffer(
    fs::FS& filesystem,
    std::string path,
    std::uint32_t maxEntries)
    : _filesystem(filesystem),
      _path(std::move(path)),
      _requestedMaxEntries(maxEntries)
{
}

RingBuffer::~RingBuffer()
{
    close();
}

Error RingBuffer::open()
{
    close();

    if (_requestedMaxEntries == 0) {
        return Error::invalid_capacity;
    }

    const bool existed = _filesystem.exists(_path.c_str());
    Error error = existed ? openExistingFile() : createNewFile();

    if (error != Error::none) {
        close();
        if (!existed) {
            _filesystem.remove(_path.c_str());
        }
        return error;
    }

    error = reconstructState();
    if (error != Error::none) {
        close();
        if (!existed) {
            _filesystem.remove(_path.c_str());
        }
    }

    return error;
}

void RingBuffer::close()
{
    if (_file) {
        _file.close();
    }

    _header = {};
    _nextId = 1;
    _count = 0;
}

bool RingBuffer::isOpen() const
{
    return static_cast<bool>(_file);
}

Error RingBuffer::createNewFile()
{
    _header = {};
    std::memcpy(_header.magic, MAGIC, sizeof(MAGIC));
    _header.version = FORMAT_VERSION;
    _header.header_size = HEADER_SIZE;
    _header.entry_size = ENTRY_SIZE;
    _header.max_entries = _requestedMaxEntries;
    _header.crc32 = calculateHeaderCrc(_header);

    _file = _filesystem.open(_path.c_str(), "w+");
    if (!_file) {
        return Error::file_create_failed;
    }

    if (_file.write(
            reinterpret_cast<const std::uint8_t*>(&_header),
            HEADER_SIZE) != HEADER_SIZE) {
        return Error::file_write_failed;
    }

    const std::size_t dataBytes =
        static_cast<std::size_t>(_header.max_entries) * ENTRY_SIZE;

    const std::array<std::uint8_t, PREALLOCATE_CHUNK_SIZE> zeros{};
    std::size_t remaining = dataBytes;

    while (remaining > 0) {
        const std::size_t chunk = std::min(remaining, zeros.size());
        if (_file.write(zeros.data(), chunk) != chunk) {
            return Error::preallocation_failed;
        }
        remaining -= chunk;
    }

    _file.flush();

    if (_file.size() != HEADER_SIZE + dataBytes) {
        return Error::preallocation_failed;
    }

    return Error::none;
}

Error RingBuffer::openExistingFile()
{
    _file = _filesystem.open(_path.c_str(), "r+");
    if (!_file) {
        return Error::file_open_failed;
    }

    if (!_file.seek(0, SeekSet)) {
        return Error::file_seek_failed;
    }

    if (_file.read(
            reinterpret_cast<std::uint8_t*>(&_header),
            HEADER_SIZE) != HEADER_SIZE) {
        return Error::file_read_failed;
    }

    if (!isValidHeader(_header)) {
        return Error::invalid_header;
    }

    const std::size_t expectedSize =
        HEADER_SIZE + static_cast<std::size_t>(_header.max_entries) * ENTRY_SIZE;

    if (_file.size() != expectedSize) {
        return Error::invalid_file_size;
    }

    return Error::none;
}

Error RingBuffer::reconstructState()
{
    std::vector<Entry> entries;
    const Error error = scanValidEntries(entries);
    if (error != Error::none) {
        return error;
    }

    _count = static_cast<std::uint32_t>(entries.size());

    if (entries.empty()) {
        _nextId = 1;
        return Error::none;
    }

    _nextId = entries.back().id == std::numeric_limits<MessageId>::max()
        ? 0
        : entries.back().id + 1;
    return Error::none;
}

WriteResult RingBuffer::writeNext(
    const std::string& handle,
    const std::string& message)
{
    WriteResult result{};

    if (!isOpen()) {
        result.error = Error::not_open;
        return result;
    }

    if (handle.empty() || handle.size() >= HANDLE_MAX_SIZE) {
        result.error = Error::invalid_handle;
        return result;
    }

    if (message.empty() || message.size() >= MESSAGE_MAX_SIZE) {
        result.error = Error::invalid_message;
        return result;
    }

    if (_nextId == 0) {
        result.error = Error::id_exhausted;
        return result;
    }

    Entry entry{};
    entry.id = _nextId;
    std::memcpy(entry.handle, handle.data(), handle.size());
    std::memcpy(entry.message, message.data(), message.size());
    entry.crc32 = calculateEntryCrc(entry);

    const std::uint32_t slot = static_cast<std::uint32_t>(
        (entry.id - 1) % _header.max_entries);

    result.error = writeSlot(slot, entry);
    if (result.error != Error::none) {
        return result;
    }

    Entry verified{};
    bool isValid = false;
    result.error = readSlot(slot, verified, isValid);
    if (result.error != Error::none) {
        return result;
    }

    if (!isValid || verified.id != entry.id) {
        result.error = Error::write_verification_failed;
        return result;
    }

    if (_nextId == std::numeric_limits<MessageId>::max()) {
        // The current record was written successfully, but no later ID exists.
        // Keep the record visible and mark the next write as exhausted.
        _nextId = 0;
    } else {
        ++_nextId;
    }

    if (_count < _header.max_entries) {
        ++_count;
    }

    result.entry = entry;
    return result;
}

Error RingBuffer::readAll(std::vector<Entry>& entries)
{
    entries.clear();

    if (!isOpen()) {
        return Error::not_open;
    }

    return scanValidEntries(entries);
}

Error RingBuffer::readAfter(
    MessageId afterId,
    std::vector<Entry>& entries)
{
    entries.clear();

    if (!isOpen()) {
        return Error::not_open;
    }

    if (_count == 0) {
        return Error::none;
    }

    const MessageId latest = latestId();
    if (afterId >= latest) {
        return Error::none;
    }

    const MessageId oldest =
        latest - static_cast<MessageId>(_count) + 1;
    const MessageId first = afterId < oldest ? oldest : afterId + 1;

    const std::size_t resultCount =
        static_cast<std::size_t>(latest - first + 1);
    entries.reserve(resultCount);

    for (MessageId id = first;; ++id) {
        const std::uint32_t slot = static_cast<std::uint32_t>(
            (id - 1) % _header.max_entries);

        Entry entry{};
        bool isValid = false;
        const Error error = readSlot(slot, entry, isValid);
        if (error != Error::none) {
            entries.clear();
            return error;
        }

        if (isValid && entry.id == id) {
            entries.push_back(entry);
        }

        if (id == latest) {
            break;
        }
    }

    return Error::none;
}

MessageId RingBuffer::latestId() const noexcept
{
    if (_count == 0) {
        return 0;
    }

    return _nextId == 0
        ? std::numeric_limits<MessageId>::max()
        : _nextId - 1;
}

std::uint32_t RingBuffer::count() const noexcept
{
    return _count;
}

std::uint32_t RingBuffer::capacity() const noexcept
{
    return _header.max_entries;
}

Error RingBuffer::scanValidEntries(std::vector<Entry>& entries)
{
    entries.clear();
    entries.reserve(_header.max_entries);

    for (std::uint32_t slot = 0; slot < _header.max_entries; ++slot) {
        Entry entry{};
        bool isValid = false;
        const Error error = readSlot(slot, entry, isValid);
        if (error != Error::none) {
            entries.clear();
            return error;
        }

        if (isValid) {
            entries.push_back(entry);
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const Entry& left, const Entry& right) {
            return left.id < right.id;
        });

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

    return Error::none;
}

Error RingBuffer::readSlot(
    std::uint32_t slot,
    Entry& entry,
    bool& isValid)
{
    entry = {};
    isValid = false;

    if (slot >= _header.max_entries) {
        return Error::slot_out_of_range;
    }

    if (!_file.seek(slotOffset(slot), SeekSet)) {
        return Error::file_seek_failed;
    }

    if (_file.read(
            reinterpret_cast<std::uint8_t*>(&entry),
            ENTRY_SIZE) != ENTRY_SIZE) {
        entry = {};
        return Error::file_read_failed;
    }

    isValid = isValidEntry(entry);
    return Error::none;
}

Error RingBuffer::writeSlot(std::uint32_t slot, const Entry& entry)
{
    if (slot >= _header.max_entries) {
        return Error::slot_out_of_range;
    }

    if (!_file.seek(slotOffset(slot), SeekSet)) {
        return Error::file_seek_failed;
    }

    if (_file.write(
            reinterpret_cast<const std::uint8_t*>(&entry),
            ENTRY_SIZE) != ENTRY_SIZE) {
        return Error::file_write_failed;
    }

    _file.flush();
    return Error::none;
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
                static_cast<std::uint32_t>(
                    -(static_cast<std::int32_t>(crc & 1u)));
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
