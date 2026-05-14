#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Esp32BaseAppConfig {
public:
    static constexpr uint16_t STRING_MAX_LENGTH = 256;
    static constexpr uint8_t ENUM_VALUE_MAX_LENGTH = 31;
    static constexpr uint8_t LABEL_MAX_LENGTH = 31;
    static constexpr uint8_t HELP_MAX_LENGTH = 96;
    static constexpr uint8_t UNIT_MAX_LENGTH = 12;
    static constexpr uint8_t DECIMAL_SCALE_MAX = 6;

    enum FieldType : uint8_t {
        FIELD_STRING,
        FIELD_INT,
        FIELD_DECIMAL,
        FIELD_BOOL,
        FIELD_ENUM
    };

    struct FieldRef {
        const char* groupId;
        const char* ns;
        const char* key;
        const char* label;
        FieldType type;
        bool restartRequired;
    };

    struct FieldValue {
        FieldType type;
        const char* text;
        int32_t raw;
        bool boolean;
    };

    struct Change {
        FieldRef field;
        FieldValue oldValue;
        FieldValue newValue;
    };

    struct SaveSummary {
        uint8_t changedCount;
        uint8_t savedCount;
        uint8_t failedCount;
        bool restartRequired;
    };

    using FieldValidateCallback = bool (*)(const FieldRef& field, const FieldValue& value, char* error, size_t errorLen);
    using PageValidateCallback = bool (*)(char* error, size_t errorLen);
    using ChangeCallback = void (*)(const Change& change);
    using SaveCallback = void (*)(const SaveSummary& summary);

    // Registration strings and EnumOption arrays are referenced, not copied.
    // Keep every const char* passed to addGroup/add* alive for the firmware lifetime
    // (normally static const storage).
    struct Group {
        const char* id;
        const char* title;
    };

    struct StringField {
        const char* groupId;
        const char* ns;
        const char* key;
        const char* label;
        const char* defaultValue;
        uint16_t minLength;
        uint16_t maxLength;
        const char* help;
        bool restartRequired;
        FieldValidateCallback validate;
    };

    struct IntField {
        const char* groupId;
        const char* ns;
        const char* key;
        const char* label;
        int32_t defaultValue;
        int32_t minValue;
        int32_t maxValue;
        int32_t step;
        const char* unit;
        const char* help;
        bool restartRequired;
        FieldValidateCallback validate;
    };

    struct DecimalField {
        const char* groupId;
        const char* ns;
        const char* key;
        const char* label;
        int32_t defaultRawValue;
        int32_t minRawValue;
        int32_t maxRawValue;
        int32_t stepRaw;
        uint8_t scale;
        const char* unit;
        const char* help;
        bool restartRequired;
        FieldValidateCallback validate;
    };

    struct BoolField {
        const char* groupId;
        const char* ns;
        const char* key;
        const char* label;
        bool defaultValue;
        const char* help;
        bool restartRequired;
        FieldValidateCallback validate;
    };

    struct EnumOption {
        const char* value;
        const char* label;
    };

    struct EnumField {
        const char* groupId;
        const char* ns;
        const char* key;
        const char* label;
        const char* defaultValue;
        const EnumOption* options;
        uint8_t optionCount;
        const char* help;
        bool restartRequired;
        FieldValidateCallback validate;
    };

    static bool setTitle(const char* title);
    static bool setPageValidateCallback(PageValidateCallback callback);
    static bool setChangeCallback(ChangeCallback callback);
    static bool setSaveCallback(SaveCallback callback);

    static bool addGroup(const Group& group);
    static bool addString(const StringField& field);
    static bool addInt(const IntField& field);
    static bool addDecimal(const DecimalField& field);
    static bool addBool(const BoolField& field);
    static bool addEnum(const EnumField& field);

    static bool submittedString(const char* ns, const char* key, char* out, size_t len);
    static bool submittedInt(const char* ns, const char* key, int32_t& out);
    static bool submittedDecimal(const char* ns, const char* key, int32_t& rawOut);
    static bool submittedBool(const char* ns, const char* key, bool& out);
    static bool submittedEnum(const char* ns, const char* key, char* out, size_t len);
};
