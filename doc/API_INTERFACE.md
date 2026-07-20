# Message in a Bottle API interface

The client uses a basic `read` and `write` endpoint structure with incremental reads.

## Message object

```json
{
  "id": 42,
  "handle": "riverstone",
  "message": "I found this on the trail."
}
```

Requirements:

- `id` is a positive, monotonically increasing integer.
- `handle` is at most 31 UTF-8 bytes.
- `message` is at most 127 UTF-8 bytes.
- Results are returned in ascending `id` order.

## Initial read

```http
GET /read
Accept: application/json
```

Response:

```json
{
  "entries": [
    {
      "id": 41,
      "handle": "moss",
      "message": "Hello from the park."
    },
    {
      "id": 42,
      "handle": "riverstone",
      "message": "I found this on the trail."
    }
  ]
}
```

The initial read returns every currently retained ring-buffer entry, oldest first.

## Incremental read

```http
GET /read?afterId=42
Accept: application/json
```

Response:

```json
{
  "entries": [
    {
      "id": 43,
      "handle": "fern",
      "message": "Adding another message."
    }
  ]
}
```

Only entries with `id > afterId` are returned. An empty update is represented as:

```json
{ "entries": [] }
```

For a ring buffer, an implementation should still return all retained entries newer than `afterId`. If `afterId` is older than the oldest retained entry, returning all currently retained entries is acceptable; the client deduplicates by ID.

## Write

```http
POST /write
Accept: application/json
Content-Type: application/json

{
  "handle": "fern",
  "message": "Adding another message."
}
```

Success response:

```json
{
  "entry": {
    "id": 43,
    "handle": "fern",
    "message": "Adding another message."
  }
}
```

The server assigns the ID. Returning the complete stored entry lets the posting client render it immediately without waiting for the next poll.

## Suggested errors

- `400 Bad Request`: malformed JSON, missing field, invalid UTF-8, or byte limit exceeded.
- `413 Content Too Large`: request body exceeds the server's maximum accepted body size.
- `500 Internal Server Error`: persistent-storage write failed.
- `503 Service Unavailable`: guestbook storage is temporarily unavailable.