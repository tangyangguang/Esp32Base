# Record Store Demo

This example stores fixed-length door-opening history records. The application encodes only its eight-byte business payload. `Esp32BaseRecordStore` adds the record ID, completion time, duration and CRC, then rotates the oldest records inside a fixed 32 KiB file.

The example intentionally writes fields at explicit byte offsets instead of persisting a C++ object. A watering project can use the same pattern for six fixed zone-detail blocks; a feeding project can create a separate store with a different payload size and file budget.

Build all representative chips:

```sh
pio run -d examples/record_store_demo -e esp32
pio run -d examples/record_store_demo -e esp32s3
pio run -d examples/record_store_demo -e esp32c3
pio run -d examples/record_store_demo -e esp32_arduino3
```
