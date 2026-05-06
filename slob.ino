#include <Arduino.h>
#include <SD.h>
#include "Slob.h"

#define CS_PIN 5

void setup() {
    Serial.begin(115200);
    
    if (!psramInit()) {
        Serial.println("PSRAM is required but failed to initialize!");
        return;
    }

    if (!SD.begin(CS_PIN)) {
        Serial.println("SD card mount failed!");
        return;
    }

    const char* filename = "/dict.slob";
    fs::File file = SD.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Failed to open dictionary file.");
        return;
    }

    Slob slob(file);
    if (!slob.begin()) {
        Serial.println("Failed to parse SLOB format.");
        return;
    }

    Serial.printf("Slob loaded! Total entries: %d\n", slob.refCount());

    const char* search_word = "hello";
    int32_t index = slob.find(search_word);

    if (index != -1) {
        BlobRef ref = slob.getRef(index);
        
        uint32_t len = 0;
        std::string mime_type;
        const uint8_t* data = slob.getBlob(ref, len, mime_type);

        if (data) {
            Serial.printf("\nFound: %s\nType: %s, Length: %d\n", ref.key.c_str(), mime_type.c_str(), len);
            
            // Data is NOT null terminated - print it safely using precision format:
            Serial.printf("Content: %.*s\n", len, data);
            // Or construct a string: String myText((char*)data, len);
        }
    } else {
        Serial.println("Word not found.");
    }
}

void loop() {
}