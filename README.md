# Slob Arduino

This is the Arduino port of [Slob data store](https://github.com/itkach/slob) targeting ESP32 with PSRAM.

It reads a Slob dictionary file (e.g. Wikipedia dump) using zlib compression from SD card, looks up an article by its key and decompresses it using miniz.h built in ESP32. It doesn't support Unicode due to ICU size.

### License

Licensed under GNU GPL 3.0.
