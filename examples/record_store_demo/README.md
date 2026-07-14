# Record Store Demo

This example stores fixed-length door-opening history records. The application encodes only its eight-byte business payload. `Esp32BaseRecordStore` adds the record ID, completion time, boot/uptime, duration and CRC, then appends and rotates complete segments inside a 32 KiB logical store budget.

The example intentionally writes fields at explicit byte offsets instead of persisting a C++ object. A watering project can use the same pattern for six fixed zone-detail blocks; a feeding project can create a separate store with a different payload size and file budget.

The store directory is `/esp32base/records/door-opening.v1/`. See [`docs/12_record_store.md`](../../docs/12_record_store.md) for capacity planning, failure behavior and measured classic ESP32 performance.

This minimal demo does not enable Web. In a Web-enabled business application, register each current-version Store after its `begin()` call so the built-in System page owns the confirmed clear operation and post-format reload:

```cpp
const bool ready = doorOpeningRecords.begin(definition);
const bool registered = Esp32BaseWeb::registerBusinessRecordStore(doorOpeningRecords);
```

Check both results. Registration stores a pointer, so keep the Store object alive after successful registration; a global, static, or application-owned long-lived member is sufficient. Register only the current active version and do not build a second business clear endpoint. The built-in System page clears registered Stores and automatically reloads them after a System-page LittleFS format.

Build all representative chips:

```sh
pio run -d examples/record_store_demo -e esp32
pio run -d examples/record_store_demo -e esp32s3
pio run -d examples/record_store_demo -e esp32c3
pio run -d examples/record_store_demo -e esp32_arduino3
```
