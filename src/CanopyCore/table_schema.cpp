#include "table_schema.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace table_schema {

const char* const kLanguageKeys[kLanguageCount] = {"ja", "en", "fr", "es", "zh-tw", "ko", "zh-cn"};

namespace {

using FT = FieldType;

// ---- Curated schemas -----------------------------------------------------
// From the table catalog and the loaders' serializers: each row starts with
// its u64 id, its enum name and a comment, and the record keeps the id at 0,
// the enum name at 8 and the comment at 48.

// name, offset, size, type, refTable, iniKey, defaultValue, count, stride,
// element, elementCount, editable, recordOffset
constexpr FieldSpec kTextureFields[] = {
    {"id", 0, 8, FT::Id, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 0},
    {"enumName", 8, 8, FT::Enum, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 8},
    {"comment", 16, 8, FT::Text, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 48},
    // The file the game opens, per language. Mod rows set all seven.
    {"fileName", 24, 56, FT::MLPath, nullptr, "file", nullptr, 0, 0, nullptr, 0, true, 88},
    // Loaded at boot and kept.
    {"isResident", 80, 4, FT::Bool, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 144},
    // Preloads <file>_thumnail.nltx, which must then exist.
    {"isThumnail", 84, 4, FT::Bool, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 148},
    {"isNoCompress", 88, 4, FT::Bool, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 152},
    // 0 in every vanilla row.
    {"width", 92, 4, FT::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 156},
    {"height", 96, 4, FT::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 160},
    {"trailer", -16, 16, FT::Trailer, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 164},
};

// One language's part of an icon atlas, in pixels: the source rect, the
// size it is drawn at and an offset.
constexpr FieldSpec kDrawInfoFields[] = {
    {"x", 0, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 0},
    {"y", 4, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 4},
    {"w", 8, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 8},
    {"h", 12, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 12},
    {"drawW", 16, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 16},
    {"drawH", 20, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 20},
    {"offsetX", 24, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 24},
    {"offsetY", 28, 4, FT::F32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 28},
};

constexpr FieldSpec kIconFields[] = {
    {"id", 0, 8, FT::Id, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 0},
    {"enumName", 8, 8, FT::Enum, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 8},
    {"comment", 16, 8, FT::Text, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 48},
    // A texture id: icon and texture ids overlap, so it never means an icon.
    {"textureID", 24, 8, FT::Ref64, "texture.dat", "texture", nullptr, 0, 0, nullptr, 0, true, 88},
    {"drawInfo", 32, 224, FT::Array, nullptr, "rect", nullptr, kLanguageCount, 32, kDrawInfoFields, 8, true, 96},
    {"trailer", -16, 16, FT::Trailer, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 320},
};

// One model texture swap: the texture name in the model, and the texture
// that replaces it.
constexpr FieldSpec kReplaceFields[] = {
    {"name", 0, 8, FT::Text, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
    {"texture", 8, 4, FT::Ref32, "texture.dat", nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
};

constexpr FieldSpec kAnimeReplaceTextureFields[] = {
    {"id", 0, 8, FT::Id, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 0},
    {"enumName", 8, 8, FT::Enum, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 8},
    {"comment", 16, 8, FT::Text, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 48},
    // A u32 count, then the elements. The row is 48 + 12 per element. The
    // record keeps 32 (replaceInfo, k_vector<ReplaceTextureInfo, 32>).
    {"entries", 24, 4, FT::Vector, nullptr, "replace", nullptr, 0, 12, kReplaceFields, 2, true, -1, 0, 32},
    {"trailer", -20, 20, FT::Trailer, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, -1},
};

template <size_t N>
constexpr size_t Count(const FieldSpec (&)[N]) {
    return N;
}

char LowerChar(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool Same(const std::string& left, const char* right) {
    size_t index = 0;
    for (; index < left.size() && right[index] != '\0'; ++index) {
        if (LowerChar(left[index]) != LowerChar(right[index])) {
            return false;
        }
    }
    return index == left.size() && right[index] == '\0';
}

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t dot = path.find('.', start);
        parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) {
            return parts;
        }
        start = dot + 1;
    }
}

// A language key or an index below limit.
bool ParseIndex(const std::string& text, uint32_t limit, bool languages, uint32_t& index) {
    if (languages) {
        for (int language = 0; language < kLanguageCount; ++language) {
            if (Same(text, kLanguageKeys[language])) {
                index = static_cast<uint32_t>(language);
                return true;
            }
        }
    }
    if (text.empty() || text.size() > 5) {
        return false;
    }
    index = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        index = index * 10 + static_cast<uint32_t>(c - '0');
    }
    return index < limit;
}

const FieldSpec* FindField(const FieldSpec* fields, size_t count, const std::string& name) {
    for (size_t index = 0; index < count; ++index) {
        if (Same(name, fields[index].name)) {
            return &fields[index];
        }
    }
    return nullptr;
}

size_t Place(const FieldSpec& field, size_t rowSize) {
    return field.offset < 0 ? rowSize - static_cast<size_t>(-field.offset) : static_cast<size_t>(field.offset);
}

bool ParseUnsigned(const std::string& text, uint64_t limit, uint64_t& value) {
    if (text.empty() || text.size() > 20) {
        return false;
    }
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (limit - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    return true;
}

bool ParseSigned(const std::string& text, int32_t& value) {
    const bool negative = !text.empty() && text[0] == '-';
    uint64_t magnitude = 0;
    if (!ParseUnsigned(negative ? text.substr(1) : text, negative ? 2147483648ull : 2147483647ull, magnitude)) {
        return false;
    }
    value = negative ? static_cast<int32_t>(-static_cast<int64_t>(magnitude)) : static_cast<int32_t>(magnitude);
    return true;
}

bool ParseSigned64(const std::string& text, int64_t& value) {
    const bool negative = !text.empty() && text[0] == '-';
    uint64_t magnitude = 0;
    if (!ParseUnsigned(negative ? text.substr(1) : text, negative ? 9223372036854775808ull : 9223372036854775807ull,
                       magnitude)) {
        return false;
    }
    value = negative ? static_cast<int64_t>(0 - magnitude) : static_cast<int64_t>(magnitude);
    return true;
}

bool ParseFloat(const std::string& text, float& value) {
    if (text.empty() || text.size() > 32) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    value = std::strtof(text.c_str(), &end);
    return end == text.c_str() + text.size() && errno == 0 && std::isfinite(value);
}

bool ParseBool(const std::string& text, uint32_t& value) {
    if (text == "1" || Same(text, "true")) {
        value = 1;
        return true;
    }
    if (text == "0" || Same(text, "false")) {
        value = 0;
        return true;
    }
    return false;
}

bool TextValid(const std::string& text) {
    if (text.size() > 1024) {
        return false;
    }
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            return false;
        }
    }
    return true;
}

bool IsText(FieldType type) {
    return type == FT::Text || type == FT::Enum;
}

void AddLocation(
    const FieldSpec& field, FieldType type, uint32_t size, size_t offset, const std::string& path,
    std::vector<Location>& out
) {
    Location location;
    location.field = &field;
    location.type = type;
    location.size = size;
    location.offset = offset;
    location.path = path;
    out.push_back(location);
}

// A plain field, or each language of a per-language text.
bool ResolveLeaf(
    const FieldSpec& field, size_t base, const std::string& prefix, const std::vector<std::string>& rest,
    std::vector<Location>& out, std::string& error
) {
    const std::string name = prefix + field.name;
    if (!field.editable) {
        error = name + " cannot be edited";
        return false;
    }
    if (field.type == FT::MLText || field.type == FT::MLPath) {
        if (rest.size() > 1) {
            error = name + " takes one language at most";
            return false;
        }
        uint32_t only = 0;
        if (!rest.empty() && !ParseIndex(rest[0], kLanguageCount, true, only)) {
            error = name + " has no language " + rest[0];
            return false;
        }
        for (uint32_t language = 0; language < static_cast<uint32_t>(kLanguageCount); ++language) {
            if (rest.empty() || language == only) {
                AddLocation(
                    field, FT::Text, static_cast<uint32_t>(blob::kTextSize), base + blob::kTextSize * language,
                    name + "." + kLanguageKeys[language], out
                );
            }
        }
        return true;
    }
    if (!rest.empty()) {
        error = name + " has no part " + rest[0];
        return false;
    }
    if (field.type == FT::Array || field.type == FT::Vector || field.type == FT::Trailer || field.type == FT::Pad) {
        error = name + " cannot be edited as a whole";
        return false;
    }
    AddLocation(field, field.type, field.size, base, name, out);
    return true;
}

// ---- Parts of variable size ----------------------------------------------

bool Variable(const FieldSpec& field);

bool AnyVariable(const FieldSpec* fields, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (Variable(fields[index])) {
            return true;
        }
    }
    return false;
}

// A Vector, or an Array or Struct that holds one at any depth.
bool Variable(const FieldSpec& field) {
    if (field.type == FT::Vector) {
        return true;
    }
    return (field.type == FT::Array || field.type == FT::Struct) && AnyVariable(field.element, field.elementCount);
}

// An element that is one value: its single field has no name.
bool OneValue(const FieldSpec& field) {
    return field.elementCount == 1 && field.element[0].name[0] == '\0';
}

// The path of a field named name inside parent: name alone at the top, and
// parent alone for an element that is one value.
std::string Child(const std::string& parent, const char* name) {
    if (parent.empty()) {
        return name;
    }
    return name[0] == '\0' ? parent : parent + "." + name;
}

void AddPlace(
    const FieldSpec& field, FieldType type, uint32_t size, size_t offset, const std::string& path, bool editable,
    std::vector<Location>& out
) {
    AddLocation(field, type, size, offset, path, out);
    out.back().editable = editable;
}

// One vector of a row, as a walk finds it.
struct VectorSpan {
    std::string path;
    const FieldSpec* field = nullptr;
    size_t countAt = 0;
    // Where each element starts, and where the last one ends.
    std::vector<size_t> elements;
    size_t end = 0;
};

// One walk through a row: where each field starts, counting the elements of
// every part of variable size before it. places and vectors, when not null,
// get every place and every vector in field order; without them no path is
// built.
struct RowWalk {
    const uint8_t* row;
    size_t size;
    std::vector<Location>* places;
    std::vector<VectorSpan>* vectors;

    bool Named() const { return places || vectors; }

    // The fields of one group: the row, an element or a struct, which is
    // fixed bytes with every vector in it empty. end is where the group
    // ends. False when a count does not fit the row.
    bool Group(
        const FieldSpec* fields, size_t count, size_t start, size_t fixed, const std::string& parent, bool editable,
        size_t& end
    ) const {
        size_t added = 0;
        for (size_t index = 0; index < count; ++index) {
            const FieldSpec& field = fields[index];
            size_t at = start + static_cast<size_t>(field.offset) + added;
            if (field.offset < 0) {
                if (static_cast<size_t>(-field.offset) > size) {
                    return false;
                }
                at = size - static_cast<size_t>(-field.offset);
            }
            size_t extra = 0;
            if (!Field(field, at, parent, editable, extra)) {
                return false;
            }
            added += extra;
            if (start + fixed + added > size) {
                return false;
            }
        }
        end = start + fixed + added;
        return true;
    }

    // Count elements of stride bytes each from start, each a group.
    bool Elements(
        const FieldSpec& field, size_t start, uint32_t count, const std::string& name, bool editable,
        VectorSpan* span, size_t& end
    ) const {
        size_t element = start;
        for (uint32_t index = 0; index < count; ++index) {
            if (span) {
                span->elements.push_back(element);
            }
            const std::string prefix = Named() ? name + "." + std::to_string(index) : std::string();
            if (!Group(field.element, field.elementCount, element, field.stride, prefix, editable, element)) {
                return false;
            }
        }
        end = element;
        return true;
    }

    // One field that starts at at. extra is what its parts of variable size
    // add to its size with every vector empty.
    bool Field(const FieldSpec& field, size_t at, const std::string& parent, bool editable, size_t& extra) const {
        static const char* const kV16[] = {"version", "region", "product", "platform"};
        static const char* const kV20[] = {"version", "disableVersion", "region", "product", "platform"};
        extra = 0;
        const std::string name = Named() ? Child(parent, field.name) : std::string();
        editable = editable && field.editable;
        switch (field.type) {
        case FT::Vector: {
            if (at + 4 > size || field.stride == 0) {
                return false;
            }
            const uint32_t count = blob::Get<uint32_t>(row, at);
            if (count > blob::kMaxPart / field.stride || static_cast<size_t>(field.stride) * count > size - at - 4) {
                return false;
            }
            if (places) {
                AddPlace(field, FT::U32, 4, at, name + ".count", false, *places);
            }
            VectorSpan span;
            size_t end = 0;
            if (!Elements(field, at + 4, count, name, editable, vectors ? &span : nullptr, end)) {
                return false;
            }
            extra = end - (at + 4);
            if (vectors) {
                span.path = name;
                span.field = &field;
                span.countAt = at;
                span.end = end;
                vectors->push_back(std::move(span));
            }
            return true;
        }
        case FT::Array: {
            size_t end = 0;
            if (!Elements(field, at, field.count, name, editable, nullptr, end)) {
                return false;
            }
            extra = end - at - static_cast<size_t>(field.stride) * field.count;
            return true;
        }
        case FT::Struct: {
            size_t end = 0;
            if (!Group(field.element, field.elementCount, at, field.size, name, editable, end)) {
                return false;
            }
            extra = end - at - field.size;
            return true;
        }
        default:
            break;
        }
        if (!places) {
            return true;
        }
        switch (field.type) {
        case FT::MLText:
        case FT::MLPath:
            for (int language = 0; language < kLanguageCount; ++language) {
                AddPlace(
                    field, FT::Text, static_cast<uint32_t>(blob::kTextSize), at + blob::kTextSize * language,
                    name + "." + kLanguageKeys[language], editable, *places
                );
            }
            break;
        case FT::Trailer: {
            const bool v20 = field.size == 20;
            const char* const* words = v20 ? kV20 : kV16;
            for (size_t word = 0; word < (v20 ? 5u : 4u); ++word) {
                AddPlace(field, FT::U32, 4, at + 4 * word, name + "." + words[word], false, *places);
            }
            break;
        }
        case FT::Pad:
            break;
        default:
            AddPlace(field, field.type, field.size, at, name, editable, *places);
            break;
        }
        return true;
    }
};

// Walks a whole row. False when it does not fit the schema.
bool WalkRow(const TableSchema& schema, const RowWalk& walk) {
    size_t end = 0;
    return walk.size >= schema.rowSize && walk.Group(schema.fields, schema.fieldCount, 0, schema.rowSize, "", true, end) &&
           end == walk.size;
}

// Where field starts in a row of rowSize bytes whose first Vector holds
// vectorCount elements. False when another part of variable size comes
// before it, since only the row says how long that part is, and for a
// Vector other than the first, whose element count only the row holds.
bool PlaceIn(const TableSchema& schema, const FieldSpec& field, uint32_t vectorCount, size_t rowSize, size_t& base) {
    if (field.offset < 0) {
        base = Place(field, rowSize);
        return true;
    }
    base = static_cast<size_t>(field.offset);
    const FieldSpec* first = VectorField(schema);
    if (field.type == FT::Vector && &field != first) {
        return false;
    }
    for (size_t index = 0; index < schema.fieldCount && &schema.fields[index] != &field; ++index) {
        const FieldSpec& before = schema.fields[index];
        if (before.offset < 0 || !Variable(before)) {
            continue;
        }
        if (&before != first || AnyVariable(first->element, first->elementCount)) {
            return false;
        }
        base += static_cast<size_t>(first->stride) * vectorCount;
    }
    return true;
}

// ---- Paths by shape ------------------------------------------------------

// What ResolvePath walks: the path as written and split, and the places.
struct PathShape {
    const std::string& written;
    std::vector<std::string> parts;
    std::vector<Location>& out;
    std::string& error;

    // owner says whose fields they are: "info has", "form elements have".
    bool In(const FieldSpec* fields, size_t count, const std::string& parent, const std::string& owner, size_t at) {
        const FieldSpec* field = FindField(fields, count, parts[at]);
        if (!field) {
            error = owner + " no field " + parts[at];
            return false;
        }
        return Field(*field, Child(parent, field->name), at + 1);
    }

    // field, named name, with the parts from at still to place in it.
    bool Field(const FieldSpec& field, const std::string& name, size_t at) {
        if (!field.editable) {
            error = name + " cannot be edited";
            return false;
        }
        const bool more = at < parts.size();
        switch (field.type) {
        case FT::Array:
        case FT::Vector:
            return Elements(field, name, at);
        case FT::Struct:
            if (!more) {
                error = name + " needs a field, such as " + name + "." + field.element[0].name;
                return false;
            }
            return In(field.element, field.elementCount, name, name + " has", at);
        case FT::MLText:
        case FT::MLPath: {
            if (parts.size() > at + 1) {
                error = name + " takes one language at most";
                return false;
            }
            uint32_t only = 0;
            if (more && !ParseIndex(parts[at], kLanguageCount, true, only)) {
                error = name + " has no language " + parts[at];
                return false;
            }
            for (uint32_t language = 0; language < static_cast<uint32_t>(kLanguageCount); ++language) {
                if (!more || language == only) {
                    AddLocation(
                        field, FT::Text, static_cast<uint32_t>(blob::kTextSize), 0,
                        name + "." + kLanguageKeys[language], out
                    );
                }
            }
            return true;
        }
        case FT::Trailer:
        case FT::Pad:
            error = name + " cannot be edited";
            return false;
        default:
            if (more) {
                error = name + " has no part " + parts[at];
                return false;
            }
            AddLocation(field, field.type, field.size, 0, name, out);
            return true;
        }
    }

    // drawInfo.en.w and drawInfo.1.w name one element; drawInfo.w every
    // element of an array. A vector's element is named by its index only.
    bool Elements(const FieldSpec& field, const std::string& name, size_t at) {
        const bool isVector = field.type == FT::Vector;
        const bool one = OneValue(field);
        const std::string example = name + ".0" + (one ? "" : std::string(".") + field.element[0].name);
        if (at == parts.size()) {
            error = name + " needs an element, such as " + example;
            return false;
        }
        uint32_t only = 0;
        const bool indexed = ParseIndex(parts[at], UINT32_MAX, !isVector && field.count == kLanguageCount, only);
        if (indexed && (isVector ? only >= MaxElements(field) : only >= field.count)) {
            error = name + " has no element " + parts[at] +
                    (isVector ? (field.capacity != 0 ? ", it holds " + std::to_string(MaxElements(field)) + " at most"
                                                     : std::string())
                              : ", it has " + std::to_string(field.count));
            return false;
        }
        if (!indexed && isVector) {
            error = written + " names every element of " + name + ", and a path names one, such as " + example;
            return false;
        }
        const size_t next = indexed ? at + 1 : at;
        for (uint32_t element = indexed ? only : 0; element < (indexed ? only + 1 : field.count); ++element) {
            const std::string inner = name + "." + std::to_string(element);
            if (one) {
                if (!Field(field.element[0], inner, next)) {
                    return false;
                }
                continue;
            }
            if (next == parts.size()) {
                error = inner + " needs an element field, such as " + inner + "." + field.element[0].name;
                return false;
            }
            if (!In(field.element, field.elementCount, inner, name + " elements have", next)) {
                return false;
            }
        }
        return true;
    }
};

// ---- Record offsets ------------------------------------------------------

int64_t RecordIn(const FieldSpec* fields, size_t count, const std::vector<std::string>& parts, size_t at);

// The record offset of the place parts name from at on, inside field.
int64_t RecordOf(const FieldSpec& field, const std::vector<std::string>& parts, size_t at) {
    if (field.recordOffset < 0 || field.type == FT::Vector) {
        return -1;
    }
    const int64_t base = field.recordOffset;
    if (field.type == FT::Struct) {
        const int64_t inner = at < parts.size() ? RecordIn(field.element, field.elementCount, parts, at) : -1;
        return inner < 0 ? -1 : base + inner;
    }
    if (field.type != FT::Array) {
        return at == parts.size() ? base : -1;
    }
    uint32_t index = 0;
    if (at >= parts.size() || !ParseIndex(parts[at], field.count, false, index)) {
        return -1;
    }
    const int64_t element = base + static_cast<int64_t>(index) * (field.recordStride ? field.recordStride : field.stride);
    const int64_t inner = OneValue(field) ? RecordOf(field.element[0], parts, at + 1)
                          : at + 1 < parts.size() ? RecordIn(field.element, field.elementCount, parts, at + 1)
                                                  : -1;
    return inner < 0 ? -1 : element + inner;
}

int64_t RecordIn(const FieldSpec* fields, size_t count, const std::vector<std::string>& parts, size_t at) {
    const FieldSpec* field = FindField(fields, count, parts[at]);
    return field ? RecordOf(*field, parts, at + 1) : -1;
}

// The shortest text strtof reads back to the same bits; %.9g always does.
std::string Shortest(float value) {
    char text[32] = {};
    for (int digits = 1; digits <= 9; ++digits) {
        std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(value));
        const float back = std::strtof(text, nullptr);
        if (std::memcmp(&back, &value, sizeof(float)) == 0) {
            break;
        }
    }
    return text;
}

}  // namespace

const TableSchema kTexture = {
    "texture.dat", "TextureData", "texture", 0x6B6A60, 116, 8, TrailerKind::V16,
    1900000000, 50000000, 100000, "TEX_ID_CANOPY_", "textures.ini",
    kTextureFields, Count(kTextureFields), 184,
};

const TableSchema kIcon = {
    "icon.dat", "IconData", "icon", 0x6C08A0, 272, 8, TrailerKind::V16,
    1950000000, 50000000, 100000, "ICON_ID_CANOPY_", "icons.ini",
    kIconFields, Count(kIconFields), 336,
};

const TableSchema kAnimeReplaceTexture = {
    "animereplacetexture.dat", "AnimeReplaceTextureData", "animereplacetexture", 0x49510, 48, 8,
    TrailerKind::V20, 2000000000, 50000000, 100000, "ANIME_REP_ID_CANOPY_", "skins.ini",
    kAnimeReplaceTextureFields, Count(kAnimeReplaceTextureFields), 1664,
};

const std::vector<const TableSchema*>& All() {
    static const std::vector<const TableSchema*> schemas = {&kTexture, &kIcon, &kAnimeReplaceTexture};
    return schemas;
}

const TableSchema* Find(const std::string& name) {
    for (const TableSchema* schema : All()) {
        if (Same(name, schema->file) || Same(name + ".dat", schema->file)) {
            return schema;
        }
    }
    return nullptr;
}

bool Curated(const TableSchema& schema) {
    return schema.idBandSize != 0 && schema.registryFile != nullptr;
}

const std::vector<const TableSchema*>& Patchable() {
    static const std::vector<const TableSchema*> schemas = [] {
        std::vector<const TableSchema*> all = All();
        all.insert(all.end(), Generated().begin(), Generated().end());
        return all;
    }();
    return schemas;
}

uint64_t BandStart(const TableSchema& schema) {
    return Curated(schema) ? schema.idBandStart : kRowBandStart;
}

uint64_t BandSize(const TableSchema& schema) {
    return Curated(schema) ? schema.idBandSize : kRowBandSize;
}

const TableSchema* FindPatchable(const std::string& name) {
    for (const TableSchema* schema : Patchable()) {
        if (Same(name, schema->file) || Same(name + ".dat", schema->file)) {
            return schema;
        }
    }
    return nullptr;
}

// ---- Rows ----------------------------------------------------------------

const FieldSpec* VectorField(const TableSchema& schema) {
    for (size_t index = 0; index < schema.fieldCount; ++index) {
        if (schema.fields[index].type == FT::Vector) {
            return &schema.fields[index];
        }
    }
    return nullptr;
}

uint32_t MaxElements(const FieldSpec& vector) {
    return vector.capacity != 0 ? std::min(vector.capacity, kMaxPathIndex + 1) : kMaxPathIndex + 1;
}

size_t RowSize(const TableSchema& schema, uint32_t vectorCount) {
    const FieldSpec* vector = VectorField(schema);
    return schema.rowSize + (vector ? static_cast<size_t>(vector->stride) * vectorCount : 0);
}

uint32_t VectorCount(const TableSchema& schema, const uint8_t* row, size_t size) {
    const FieldSpec* vector = VectorField(schema);
    if (!vector || size < static_cast<size_t>(vector->offset) + 4) {
        return 0;
    }
    return blob::Get<uint32_t>(row, static_cast<size_t>(vector->offset));
}

bool RowFits(const TableSchema& schema, const uint8_t* row, size_t size) {
    return WalkRow(schema, RowWalk{row, size, nullptr, nullptr});
}

std::vector<uint8_t> NewRow(const TableSchema& schema, uint32_t vectorCount, blob::Pool& pool) {
    if (!Curated(schema)) {
        return {};
    }
    std::vector<uint8_t> row(RowSize(schema, vectorCount), 0);
    for (size_t index = 0; index < schema.fieldCount; ++index) {
        const FieldSpec& field = schema.fields[index];
        if (field.type == FT::Vector) {
            blob::Put<uint32_t>(row.data(), static_cast<size_t>(field.offset), vectorCount);
            continue;
        }
        // Every text points at a NUL, so an unset text reads as empty.
        if (IsText(field.type)) {
            pool.Write(row, Place(field, row.size()), field.defaultValue ? field.defaultValue : "");
            continue;
        }
        if (field.type == FT::MLText || field.type == FT::MLPath) {
            for (int language = 0; language < kLanguageCount; ++language) {
                pool.Write(
                    row, Place(field, row.size()) + blob::kTextSize * language,
                    field.defaultValue ? field.defaultValue : ""
                );
            }
            continue;
        }
        if (field.defaultValue) {
            std::vector<Location> locations;
            std::string error;
            if (Resolve(schema, field.name, vectorCount, row.size(), locations, error)) {
                for (const Location& location : locations) {
                    Write(row, location, field.defaultValue, pool, error);
                }
            }
        }
    }
    const FieldSpec* vector = VectorField(schema);
    if (vector) {
        for (uint32_t element = 0; element < vectorCount; ++element) {
            const size_t base = static_cast<size_t>(vector->offset) + 4 + static_cast<size_t>(vector->stride) * element;
            for (uint32_t index = 0; index < vector->elementCount; ++index) {
                if (IsText(vector->element[index].type)) {
                    pool.Write(row, base + static_cast<size_t>(vector->element[index].offset), "");
                }
            }
        }
    }
    return row;
}

bool Resize(const TableSchema& schema, std::vector<uint8_t>& row, uint32_t count) {
    const FieldSpec* vector = VectorField(schema);
    if (!vector || AnyVariable(vector->element, vector->elementCount) || !RowFits(schema, row.data(), row.size())) {
        return false;
    }
    const uint32_t old = VectorCount(schema, row.data(), row.size());
    const size_t end = static_cast<size_t>(vector->offset) + 4 + static_cast<size_t>(vector->stride) * old;
    if (count > old) {
        row.insert(row.begin() + static_cast<std::ptrdiff_t>(end), static_cast<size_t>(vector->stride) * (count - old), 0);
    } else if (count < old) {
        const size_t keep = static_cast<size_t>(vector->offset) + 4 + static_cast<size_t>(vector->stride) * count;
        row.erase(row.begin() + static_cast<std::ptrdiff_t>(keep), row.begin() + static_cast<std::ptrdiff_t>(end));
    }
    blob::Put<uint32_t>(row.data(), static_cast<size_t>(vector->offset), count);
    return true;
}

// ---- Fields --------------------------------------------------------------

bool Resolve(
    const TableSchema& schema, const std::string& path, uint32_t vectorCount, size_t rowSize,
    std::vector<Location>& out, std::string& error
) {
    out.clear();
    const std::vector<std::string> parts = SplitPath(path);
    const FieldSpec* field = FindField(schema.fields, schema.fieldCount, parts[0]);
    if (!field) {
        error = std::string(schema.file) + " has no field " + parts[0];
        return false;
    }
    if (rowSize < schema.rowSize) {
        error = "the row is shorter than " + std::to_string(schema.rowSize) + " bytes";
        return false;
    }
    if (!field->editable) {
        error = std::string(field->name) + " cannot be edited";
        return false;
    }
    if (field->element && AnyVariable(field->element, field->elementCount)) {
        error = std::string(field->name) + " has elements of different sizes, so only the row can place them";
        return false;
    }
    size_t base = 0;
    if (!PlaceIn(schema, *field, vectorCount, rowSize, base)) {
        error = std::string(field->name) + " comes after a part of variable size, so only the row can place it";
        return false;
    }
    const std::vector<std::string> rest(parts.begin() + 1, parts.end());
    if (field->type != FT::Array && field->type != FT::Vector) {
        return ResolveLeaf(*field, base, "", rest, out, error);
    }
    const bool isVector = field->type == FT::Vector;
    const uint32_t count = isVector ? vectorCount : field->count;
    const size_t first = base + (isVector ? 4 : 0);
    if (rest.empty()) {
        error = std::string(field->name) + " needs an element field, such as " + field->name + ".0." +
                field->element[0].name;
        return false;
    }
    // drawInfo.en.w and drawInfo.1.w name one element; drawInfo.w all.
    uint32_t only = 0;
    const bool one = ParseIndex(rest[0], UINT32_MAX, !isVector && field->count == kLanguageCount, only);
    if (one && only >= count) {
        error = std::string(field->name) + " has no element " + rest[0] + ", it has " + std::to_string(count);
        return false;
    }
    const std::vector<std::string> tail(rest.begin() + (one ? 1 : 0), rest.end());
    if (tail.empty()) {
        error = std::string(field->name) + "." + rest[0] + " needs an element field";
        return false;
    }
    const FieldSpec* inner = FindField(field->element, field->elementCount, tail[0]);
    if (!inner) {
        error = std::string(field->name) + " elements have no field " + tail[0];
        return false;
    }
    const std::vector<std::string> innerRest(tail.begin() + 1, tail.end());
    for (uint32_t element = 0; element < count; ++element) {
        if (one && element != only) {
            continue;
        }
        const size_t start = first + static_cast<size_t>(field->stride) * element;
        if (!ResolveLeaf(
                *inner, start + static_cast<size_t>(inner->offset),
                std::string(field->name) + "." + std::to_string(element) + ".", innerRest, out, error
            )) {
            return false;
        }
    }
    if (out.empty()) {
        error = std::string(field->name) + " has no elements";
        return false;
    }
    return true;
}

bool Layout(
    const TableSchema& schema, const uint8_t* row, size_t size, std::vector<Location>& out, std::string& error
) {
    out.clear();
    if (!WalkRow(schema, RowWalk{row, size, &out, nullptr})) {
        out.clear();
        error = "a row of " + std::to_string(size) + " bytes does not fit " + schema.file;
        return false;
    }
    return true;
}

bool ResolvePath(const TableSchema& schema, const std::string& path, std::vector<Location>& out, std::string& error) {
    out.clear();
    PathShape shape{path, SplitPath(path), out, error};
    if (!shape.In(schema.fields, schema.fieldCount, "", std::string(schema.file) + " has", 0)) {
        out.clear();
        return false;
    }
    return true;
}

bool Locate(const std::vector<Location>& layout, const std::string& path, Location& out, std::string& error) {
    for (const Location& place : layout) {
        if (place.path != path) {
            continue;
        }
        if (!place.editable) {
            error = path + " cannot be edited";
            return false;
        }
        out = place;
        return true;
    }
    // Name the element the row lacks: form.3 of a row with 2 forms.
    const std::vector<std::string> parts = SplitPath(path);
    std::string container = parts[0];
    for (size_t at = 1; at < parts.size(); ++at) {
        uint32_t index = 0;
        if (ParseIndex(parts[at], UINT32_MAX, false, index)) {
            uint32_t count = 0;
            const auto inside = [&](const Location& place, uint32_t element) {
                const std::string prefix = container + "." + std::to_string(element);
                return place.path == prefix || place.path.compare(0, prefix.size() + 1, prefix + ".") == 0;
            };
            while (std::any_of(layout.begin(), layout.end(), [&](const Location& place) { return inside(place, count); })) {
                ++count;
            }
            if (index >= count) {
                error = container + " has no element " + parts[at] + ", it has " + std::to_string(count);
                return false;
            }
        }
        container += "." + parts[at];
    }
    error = path + " is not in the row";
    return false;
}

bool ResolveRow(
    const TableSchema& schema, const std::string& path, const uint8_t* row, size_t size, std::vector<Location>& out,
    std::string& error
) {
    out.clear();
    std::vector<Location> shaped;
    std::vector<Location> layout;
    if (!ResolvePath(schema, path, shaped, error) || !Layout(schema, row, size, layout, error)) {
        return false;
    }
    for (const Location& place : shaped) {
        Location placed;
        if (!Locate(layout, place.path, placed, error)) {
            out.clear();
            return false;
        }
        out.push_back(placed);
    }
    return true;
}

bool VectorPath(const TableSchema& schema, const std::string& path, std::string& error, const FieldSpec** vector) {
    const std::vector<std::string> parts = SplitPath(path);
    const FieldSpec* fields = schema.fields;
    size_t count = schema.fieldCount;
    std::string owner = std::string(schema.file) + " has";
    std::string name;
    for (size_t at = 0; at < parts.size(); ++at) {
        const FieldSpec* field = FindField(fields, count, parts[at]);
        if (!field) {
            error = owner + " no field " + parts[at];
            return false;
        }
        name = Child(name, field->name);
        if (!field->editable) {
            error = name + " cannot be edited";
            return false;
        }
        const bool last = at + 1 == parts.size();
        if (field->type == FT::Vector && last) {
            if (vector) {
                *vector = field;
            }
            return true;
        }
        if (field->type == FT::Struct && !last) {
            fields = field->element;
            count = field->elementCount;
            owner = name + " has";
            continue;
        }
        if ((field->type != FT::Array && field->type != FT::Vector) || last) {
            error = name + " is not a list";
            return false;
        }
        const bool isVector = field->type == FT::Vector;
        uint32_t index = 0;
        if (!ParseIndex(parts[at + 1], UINT32_MAX, false, index) ||
            (isVector ? index >= MaxElements(*field) : index >= field->count)) {
            error = name + " has no element " + parts[at + 1];
            return false;
        }
        ++at;
        name += "." + std::to_string(index);
        if (at + 1 == parts.size() || OneValue(*field)) {
            error = name + " is an element, not a list";
            return false;
        }
        fields = field->element;
        count = field->elementCount;
        owner = std::string(field->name) + " elements have";
    }
    error = path + " is not a list";
    return false;
}

int64_t RecordOffset(const TableSchema& schema, const std::string& path) {
    return RecordIn(schema.fields, schema.fieldCount, SplitPath(path), 0);
}

bool ResizeVector(
    const TableSchema& schema, std::vector<uint8_t>& row, const std::string& path, uint32_t count, blob::Pool& pool,
    std::string& error
) {
    std::vector<VectorSpan> vectors;
    if (!WalkRow(schema, RowWalk{row.data(), row.size(), nullptr, &vectors})) {
        error = "a row of " + std::to_string(row.size()) + " bytes does not fit " + schema.file;
        return false;
    }
    const auto span = std::find_if(vectors.begin(), vectors.end(), [&](const VectorSpan& vector) {
        return Same(path, vector.path.c_str());
    });
    if (span == vectors.end()) {
        error = std::string(schema.file) + " rows have no vector " + path + " here";
        return false;
    }
    if (count > MaxElements(*span->field)) {
        error = span->path + " can have " + std::to_string(MaxElements(*span->field)) + " elements at most";
        return false;
    }
    const std::string name = span->path;
    const uint32_t old = static_cast<uint32_t>(span->elements.size());
    const size_t stride = span->field->stride;
    blob::Put<uint32_t>(row.data(), span->countAt, count);
    if (count < old) {
        row.erase(
            row.begin() + static_cast<std::ptrdiff_t>(span->elements[count]),
            row.begin() + static_cast<std::ptrdiff_t>(span->end)
        );
        return true;
    }
    row.insert(row.begin() + static_cast<std::ptrdiff_t>(span->end), stride * (count - old), 0);
    // Every text in the new elements points at a NUL, so it reads as empty.
    std::vector<Location> layout;
    if (count > old && Layout(schema, row.data(), row.size(), layout, error)) {
        for (uint32_t element = old; element < count; ++element) {
            const std::string prefix = name + "." + std::to_string(element);
            for (const Location& place : layout) {
                const bool inside =
                    place.path == prefix || place.path.compare(0, prefix.size() + 1, prefix + ".") == 0;
                if (inside && IsText(place.type)) {
                    pool.Write(row, place.offset, "");
                }
            }
        }
    }
    return true;
}

bool ElementCount(
    const TableSchema& schema, const uint8_t* row, size_t size, const std::string& path, uint32_t& count,
    std::string& error
) {
    std::vector<VectorSpan> vectors;
    if (!WalkRow(schema, RowWalk{row, size, nullptr, &vectors})) {
        error = "a row of " + std::to_string(size) + " bytes does not fit " + schema.file;
        return false;
    }
    for (const VectorSpan& vector : vectors) {
        if (Same(path, vector.path.c_str())) {
            count = static_cast<uint32_t>(vector.elements.size());
            return true;
        }
    }
    error = std::string(schema.file) + " rows have no vector " + path + " here";
    return false;
}

bool AppendElement(
    const TableSchema& schema, std::vector<uint8_t>& row, const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& values, blob::Pool& pool, uint32_t& index,
    std::string& error
) {
    uint32_t count = 0;
    if (!ElementCount(schema, row.data(), row.size(), path, count, error)) {
        return false;
    }
    // Built on a copy, so a value that does not fit leaves row as it was.
    std::vector<uint8_t> bytes = row;
    if (!ResizeVector(schema, bytes, path, count + 1, pool, error)) {
        return false;
    }
    const std::string element = path + "." + std::to_string(count);
    const std::string countSuffix = ".count";
    for (const auto& [key, value] : values) {
        const std::string where = element + "." + key;
        const bool countKey = key.size() > countSuffix.size() &&
                              Same(key.substr(key.size() - countSuffix.size()), countSuffix.c_str());
        const std::string list = countKey ? where.substr(0, where.size() - countSuffix.size()) : std::string();
        std::string why;
        if (countKey && VectorPath(schema, list, why)) {
            uint32_t elements = 0;
            if (!ParseIndex(value, kMaxPathIndex + 2, false, elements)) {
                error = where + ": " + value + " is not a count";
                return false;
            }
            // Its message names the list by its full path.
            if (!ResizeVector(schema, bytes, list, elements, pool, error)) {
                return false;
            }
            continue;
        }
        std::vector<Location> locations;
        if (!ResolveRow(schema, where, bytes.data(), bytes.size(), locations, why) || locations.empty()) {
            error = where + ": " + why;
            return false;
        }
        for (const Location& location : locations) {
            if (!Write(bytes, location, value, pool, why)) {
                error = where + ": " + why;
                return false;
            }
        }
    }
    row = std::move(bytes);
    index = count;
    return true;
}

bool Check(const Location& location, const std::string& value, std::string& error) {
    uint64_t number = 0;
    int32_t signedNumber = 0;
    int64_t wideNumber = 0;
    uint32_t flag = 0;
    float real = 0;
    bool valid = false;
    const char* expected = "";
    switch (location.type) {
    case FT::Text:
    case FT::Enum:
        valid = TextValid(value);
        expected = "one line of printable text";
        break;
    case FT::I32:
    case FT::FlagRef:
        valid = ParseSigned(value, signedNumber);
        expected = "a whole number in the int32 range";
        break;
    case FT::U32:
    case FT::Ref32:
        valid = ParseUnsigned(value, UINT32_MAX, number);
        expected = "a whole number from 0 to 4294967295";
        break;
    case FT::U64:
    case FT::Ref64:
    case FT::Id:
        valid = ParseUnsigned(value, UINT64_MAX, number);
        expected = "a whole number of at least 0";
        break;
    case FT::I64:
        valid = ParseSigned64(value, wideNumber);
        expected = "a whole number in the int64 range";
        break;
    case FT::F32:
        valid = ParseFloat(value, real);
        expected = "a number";
        break;
    case FT::Bool:
        valid = ParseBool(value, flag);
        expected = "0, 1, true or false";
        break;
    default:
        error = location.path + " cannot be written";
        return false;
    }
    if (!valid) {
        error = location.path + " must be " + expected;
    }
    return valid;
}

bool Write(
    std::vector<uint8_t>& row, const Location& location, const std::string& value, blob::Pool& pool,
    std::string& error
) {
    if (location.offset + location.size > row.size()) {
        error = location.path + " is outside the row";
        return false;
    }
    if (!Check(location, value, error)) {
        return false;
    }
    uint8_t* at = row.data();
    uint64_t number = 0;
    int32_t signedNumber = 0;
    int64_t wideNumber = 0;
    uint32_t flag = 0;
    float real = 0;
    switch (location.type) {
    case FT::Text:
    case FT::Enum:
        pool.Write(row, location.offset, value);
        break;
    case FT::I32:
    case FT::FlagRef:
        ParseSigned(value, signedNumber);
        blob::Put<int32_t>(at, location.offset, signedNumber);
        break;
    case FT::U32:
    case FT::Ref32:
        ParseUnsigned(value, UINT32_MAX, number);
        blob::Put<uint32_t>(at, location.offset, static_cast<uint32_t>(number));
        break;
    case FT::U64:
    case FT::Ref64:
    case FT::Id:
        ParseUnsigned(value, UINT64_MAX, number);
        blob::Put<uint64_t>(at, location.offset, number);
        break;
    case FT::I64:
        ParseSigned64(value, wideNumber);
        blob::Put<int64_t>(at, location.offset, wideNumber);
        break;
    case FT::F32:
        ParseFloat(value, real);
        blob::Put<float>(at, location.offset, real);
        break;
    case FT::Bool:
        ParseBool(value, flag);
        if (location.size == 1) {
            at[location.offset] = static_cast<uint8_t>(flag);
        } else {
            blob::Put<uint32_t>(at, location.offset, flag);
        }
        break;
    default:
        return false;
    }
    return true;
}

bool Read(
    const uint8_t* row, size_t rowSize, const Location& location, const blob::Table& table, const blob::Pool* added,
    std::string& value, std::string& error
) {
    value.clear();
    if (location.size == 0 || location.offset + location.size > rowSize) {
        error = location.path + " is outside the row";
        return false;
    }
    switch (location.type) {
    case FT::Text:
    case FT::Enum: {
        const uint32_t start = blob::Get<uint32_t>(row, location.offset);
        const uint32_t length = blob::Get<uint32_t>(row, location.offset + 4);
        if (added && start >= added->Base()) {
            const size_t at = start - added->Base();
            if (at + length > added->Bytes().size()) {
                error = location.path + " points past the added texts";
                return false;
            }
            value = added->Bytes().substr(at, length);
        } else if (static_cast<uint64_t>(start) + length <= table.poolBytes) {
            value.assign(reinterpret_cast<const char*>(table.pool) + start, length);
        } else {
            error = location.path + " points past the table's texts";
            return false;
        }
        break;
    }
    case FT::I32:
    case FT::FlagRef:
        value = std::to_string(blob::Get<int32_t>(row, location.offset));
        break;
    case FT::U32:
    case FT::Ref32:
        value = std::to_string(blob::Get<uint32_t>(row, location.offset));
        break;
    case FT::U64:
    case FT::Ref64:
    case FT::Id:
        value = std::to_string(blob::Get<uint64_t>(row, location.offset));
        break;
    case FT::I64:
        value = std::to_string(blob::Get<int64_t>(row, location.offset));
        break;
    case FT::F32:
        value = Shortest(blob::Get<float>(row, location.offset));
        break;
    case FT::Bool:
        value = std::to_string(ReadNumber(row, location));
        break;
    default:
        error = location.path + " is not one value";
        return false;
    }
    std::string why;
    if (!Check(location, value, why)) {
        error = why + ", so the value it holds cannot be written back";
        return false;
    }
    return true;
}

uint64_t ReadNumber(const uint8_t* row, const Location& location) {
    switch (location.type) {
    case FT::I32:
    case FT::FlagRef:
        return static_cast<uint64_t>(static_cast<int64_t>(blob::Get<int32_t>(row, location.offset)));
    case FT::U32:
    case FT::Ref32:
        return blob::Get<uint32_t>(row, location.offset);
    case FT::U64:
    case FT::Ref64:
    case FT::Id:
        return blob::Get<uint64_t>(row, location.offset);
    case FT::I64:
        return static_cast<uint64_t>(blob::Get<int64_t>(row, location.offset));
    case FT::Bool:
        return location.size == 1 ? row[location.offset] : blob::Get<uint32_t>(row, location.offset);
    default:
        return 0;
    }
}

}  // namespace table_schema
