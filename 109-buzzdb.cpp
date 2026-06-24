#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// BuzzDB v109: BuzzDB core inside the search simulator.
//
// This version keeps the v108 search simulator but brings back BuzzDB's
// database vocabulary: typed fields, tuples, schemas, tables, and a small
// page-file storage stack. Simulator nodes still use the in-memory mode by
// default; demos and restart tests can opt into a node-local buzzdb.dat.

struct Address {
    std::string id;

    explicit Address(std::string value = "") : id(std::move(value)) {}

    Address rootAddress() const {
        return *this;
    }

    std::string str() const {
        return id;
    }

    bool operator<(const Address& other) const {
        return id < other.id;
    }

    bool operator==(const Address& other) const {
        return id == other.id;
    }
};

std::ostream& operator<<(std::ostream& out, const Address& address) {
    out << address.str();
    return out;
}

struct CreateTableCommand {
    std::string table;
    std::vector<std::string> columns;
};

struct InsertRowCommand {
    std::string table;
    std::vector<std::string> values;
};

struct DeleteRowsCommand {
    std::string table;
    std::string column;
    std::string value;
};

struct UpdateRowsCommand {
    std::string table;
    std::string set_column;
    std::string set_value;
    std::string where_column;
    std::string where_value;
};

struct SelectAllCommand {
    std::string table;
};

struct SelectWhereCommand {
    std::string table;
    std::string column;
    std::string value;
};

struct CountRowsCommand {
    std::string table;
};

using Command = std::variant<
    CreateTableCommand,
    InsertRowCommand,
    DeleteRowsCommand,
    UpdateRowsCommand,
    SelectAllCommand,
    SelectWhereCommand,
    CountRowsCommand>;

struct CreateTableOkResult {};

struct InsertOkResult {};

struct DeleteRowsResult {
    size_t count;
};

struct UpdateRowsResult {
    size_t count;
};

struct SelectAllResult {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

struct CountRowsResult {
    size_t count;
};

struct TableNotFoundResult {};

struct TableAlreadyExistsResult {};

struct SchemaMismatchResult {};

struct InvalidSchemaResult {};

using Result = std::variant<
    CreateTableOkResult,
    InsertOkResult,
    DeleteRowsResult,
    UpdateRowsResult,
    SelectAllResult,
    CountRowsResult,
    TableNotFoundResult,
    TableAlreadyExistsResult,
    SchemaMismatchResult,
    InvalidSchemaResult>;

bool operator==(const CreateTableOkResult&, const CreateTableOkResult&) {
    return true;
}

bool operator==(const InsertOkResult&, const InsertOkResult&) {
    return true;
}

bool operator==(const DeleteRowsResult& lhs, const DeleteRowsResult& rhs) {
    return lhs.count == rhs.count;
}

bool operator==(const UpdateRowsResult& lhs, const UpdateRowsResult& rhs) {
    return lhs.count == rhs.count;
}

bool operator==(const SelectAllResult& lhs, const SelectAllResult& rhs) {
    return lhs.columns == rhs.columns && lhs.rows == rhs.rows;
}

bool operator==(const CountRowsResult& lhs, const CountRowsResult& rhs) {
    return lhs.count == rhs.count;
}

bool operator==(const TableNotFoundResult&, const TableNotFoundResult&) {
    return true;
}

bool operator==(const TableAlreadyExistsResult&,
                const TableAlreadyExistsResult&) {
    return true;
}

bool operator==(const SchemaMismatchResult&, const SchemaMismatchResult&) {
    return true;
}

bool operator==(const InvalidSchemaResult&, const InvalidSchemaResult&) {
    return true;
}

bool operator==(const Result& lhs, const Result& rhs) {
    return lhs.index() == rhs.index() &&
           std::visit(
               [](const auto& l, const auto& r) {
                   using L = std::decay_t<decltype(l)>;
                   using R = std::decay_t<decltype(r)>;
                   if constexpr (std::is_same_v<L, R>) {
                       return l == r;
                   }
                   return false;
               },
               lhs,
               rhs);
}

std::string describeCommand(const Command& command) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, CreateTableCommand>) {
                out << "CreateTable(" << value.table << ", columns=[";
                for (size_t i = 0; i < value.columns.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << value.columns[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, InsertRowCommand>) {
                out << "InsertRow(" << value.table << ", values=[";
                for (size_t i = 0; i < value.values.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << value.values[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, DeleteRowsCommand>) {
                out << "DeleteRows(" << value.table << ", "
                    << value.column << "=" << value.value << ")";
            } else if constexpr (std::is_same_v<T, UpdateRowsCommand>) {
                out << "UpdateRows(" << value.table << ", "
                    << value.set_column << "=" << value.set_value
                    << " where " << value.where_column << "="
                    << value.where_value << ")";
            } else if constexpr (std::is_same_v<T, SelectAllCommand>) {
                out << "SELECT * FROM " << value.table;
            } else if constexpr (std::is_same_v<T, SelectWhereCommand>) {
                out << "SELECT * FROM " << value.table
                    << " WHERE " << value.column << "=" << value.value;
            } else if constexpr (std::is_same_v<T, CountRowsCommand>) {
                out << "CountRows(" << value.table << ")";
            }
            return out.str();
        },
        command);
}

std::string describeResult(const Result& result) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, CreateTableOkResult>) {
                out << "CreateTableOk";
            } else if constexpr (std::is_same_v<T, InsertOkResult>) {
                out << "InsertOk";
            } else if constexpr (std::is_same_v<T, DeleteRowsResult>) {
                out << "DeleteRows(" << value.count << ")";
            } else if constexpr (std::is_same_v<T, UpdateRowsResult>) {
                out << "UpdateRows(" << value.count << ")";
            } else if constexpr (std::is_same_v<T, SelectAllResult>) {
                out << "SelectAll(columns=" << value.columns.size()
                    << ", rows=" << value.rows.size() << ")";
            } else if constexpr (std::is_same_v<T, CountRowsResult>) {
                out << "CountRows(" << value.count << ")";
            } else if constexpr (std::is_same_v<T, TableNotFoundResult>) {
                out << "TableNotFound";
            } else if constexpr (std::is_same_v<T, TableAlreadyExistsResult>) {
                out << "TableAlreadyExists";
            } else if constexpr (std::is_same_v<T, SchemaMismatchResult>) {
                out << "SchemaMismatch";
            } else if constexpr (std::is_same_v<T, InvalidSchemaResult>) {
                out << "InvalidSchema";
            }
            return out.str();
        },
        result);
}

class Application {
public:
    virtual ~Application() = default;
    virtual Result execute(const Command& command) = 0;
    virtual std::unique_ptr<Application> clone() const = 0;
    virtual std::string digest() const = 0;
};

enum FieldType { INT, FLOAT, STRING };

std::string fieldTypeName(FieldType type) {
    switch (type) {
        case INT:
            return "INT";
        case FLOAT:
            return "FLOAT";
        case STRING:
            return "STRING";
    }
    throw std::runtime_error("Unknown field type.");
}

std::string trimCopy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() &&
           std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }

    size_t end = input.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }

    return input.substr(start, end - start);
}

std::string stripOptionalSemicolon(const std::string& input) {
    std::string trimmed = trimCopy(input);
    if (!trimmed.empty() && trimmed.back() == ';') {
        trimmed.pop_back();
    }
    return trimCopy(trimmed);
}

std::vector<std::string> splitCommaList(const std::string& input) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, ',')) {
        tokens.push_back(trimCopy(token));
    }
    return tokens;
}

std::vector<std::string> splitPipeLine(const std::string& input) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, '|')) {
        tokens.push_back(trimCopy(token));
    }
    return tokens;
}

std::string lowerCopy(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return input;
}

FieldType parseFieldTypeName(const std::string& input) {
    std::string type = lowerCopy(trimCopy(input));
    if (type == "int" || type == "integer") {
        return INT;
    }
    if (type == "float" || type == "real") {
        return FLOAT;
    }
    if (type == "string" || type == "text") {
        return STRING;
    }
    throw std::invalid_argument("Unknown field type: " + input);
}

class Field {
public:
    FieldType type;
    size_t data_length;
    std::unique_ptr<char[]> data;

    explicit Field(int value) : type(INT), data_length(sizeof(int)) {
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &value, data_length);
    }

    explicit Field(float value) : type(FLOAT), data_length(sizeof(float)) {
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &value, data_length);
    }

    explicit Field(const std::string& value)
        : type(STRING), data_length(value.size() + 1) {
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), value.c_str(), data_length);
    }

    Field(const Field& other)
        : type(other.type), data_length(other.data_length) {
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), other.data.get(), data_length);
    }

    Field(Field&& other) noexcept = default;

    Field& operator=(const Field& other) {
        if (this == &other) {
            return *this;
        }
        type = other.type;
        data_length = other.data_length;
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), other.data.get(), data_length);
        return *this;
    }

    Field& operator=(Field&& other) noexcept = default;

    FieldType getType() const {
        return type;
    }

    int asInt() const {
        int value = 0;
        std::memcpy(&value, data.get(), sizeof(int));
        return value;
    }

    float asFloat() const {
        float value = 0.0f;
        std::memcpy(&value, data.get(), sizeof(float));
        return value;
    }

    std::string asString() const {
        return std::string(data.get());
    }

    std::unique_ptr<Field> clone() const {
        return std::make_unique<Field>(*this);
    }

    std::string serialize() const {
        std::ostringstream buffer;
        buffer << type << ' ' << data_length << ' ';
        switch (type) {
            case INT:
                buffer << asInt();
                break;
            case FLOAT:
                buffer << asFloat();
                break;
            case STRING:
                buffer << asString();
                break;
        }
        return buffer.str();
    }

    std::string toString() const {
        std::ostringstream out;
        switch (type) {
            case INT:
                out << asInt();
                break;
            case FLOAT:
                out << asFloat();
                break;
            case STRING:
                out << asString();
                break;
        }
        return out.str();
    }
};

bool operator==(const Field& lhs, const Field& rhs) {
    if (lhs.getType() != rhs.getType()) {
        return false;
    }

    switch (lhs.getType()) {
        case INT:
            return lhs.asInt() == rhs.asInt();
        case FLOAT:
            return lhs.asFloat() == rhs.asFloat();
        case STRING:
            return lhs.asString() == rhs.asString();
    }
    throw std::runtime_error("Unknown field type.");
}

std::string fieldToString(const Field& field) {
    return field.toString();
}

Field parseLiteralField(FieldType type, const std::string& token) {
    switch (type) {
        case INT:
            return Field(std::stoi(token));
        case FLOAT:
            return Field(std::stof(token));
        case STRING:
            return Field(token);
    }
    throw std::runtime_error("Unknown field type.");
}

class Tuple {
public:
    std::vector<std::unique_ptr<Field>> fields;

    Tuple() = default;

    Tuple(const Tuple& other) {
        fields.reserve(other.fields.size());
        for (const auto& field : other.fields) {
            fields.push_back(field->clone());
        }
    }

    Tuple(Tuple&& other) noexcept = default;

    Tuple& operator=(const Tuple& other) {
        if (this == &other) {
            return *this;
        }
        fields.clear();
        fields.reserve(other.fields.size());
        for (const auto& field : other.fields) {
            fields.push_back(field->clone());
        }
        return *this;
    }

    Tuple& operator=(Tuple&& other) noexcept = default;

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
    }

    size_t size() const {
        return fields.size();
    }

    size_t getSize() const {
        size_t size_bytes = 0;
        for (const auto& field : fields) {
            size_bytes += field->data_length;
        }
        return size_bytes;
    }

    std::vector<std::string> toStrings() const {
        std::vector<std::string> values;
        values.reserve(fields.size());
        for (const auto& field : fields) {
            values.push_back(fieldToString(*field));
        }
        return values;
    }

    std::string serialize() const {
        std::ostringstream buffer;
        buffer << fields.size() << ' ';
        for (const auto& field : fields) {
            buffer << field->serialize() << ' ';
        }
        return buffer.str();
    }

    std::unique_ptr<Tuple> clone() const {
        return std::make_unique<Tuple>(*this);
    }
};

struct ColumnSchema {
    std::string name;
    FieldType type;
};

class Schema {
public:
    Schema() = default;

    explicit Schema(std::vector<ColumnSchema> columns)
        : columns_(std::move(columns)) {}

    static Schema fromColumnSpecs(const std::vector<std::string>& specs) {
        if (specs.empty()) {
            throw std::invalid_argument("table schema must have columns");
        }

        std::set<std::string> seen_names;
        std::vector<ColumnSchema> columns;
        columns.reserve(specs.size());
        for (const auto& spec : specs) {
            ColumnSchema column = parseColumnSpec(spec);
            if (column.name.empty()) {
                throw std::invalid_argument("column name must not be empty");
            }
            if (!seen_names.insert(column.name).second) {
                throw std::invalid_argument("duplicate column: " + column.name);
            }
            columns.push_back(std::move(column));
        }
        return Schema(std::move(columns));
    }

    static ColumnSchema parseColumnSpec(const std::string& spec) {
        size_t separator = spec.find(':');
        if (separator == std::string::npos) {
            return ColumnSchema{trimCopy(spec), STRING};
        }
        std::string name = trimCopy(spec.substr(0, separator));
        FieldType type = parseFieldTypeName(spec.substr(separator + 1));
        return ColumnSchema{name, type};
    }

    size_t size() const {
        return columns_.size();
    }

    const std::vector<ColumnSchema>& columns() const {
        return columns_;
    }

    std::vector<std::string> columnNames() const {
        std::vector<std::string> names;
        names.reserve(columns_.size());
        for (const auto& column : columns_) {
            names.push_back(column.name);
        }
        return names;
    }

    std::optional<size_t> columnIndex(const std::string& name) const {
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<FieldType> columnType(const std::string& name) const {
        std::optional<size_t> index = columnIndex(name);
        if (!index.has_value()) {
            return std::nullopt;
        }
        return columns_[*index].type;
    }

    bool matches(const Tuple& tuple) const {
        if (tuple.size() != columns_.size()) {
            return false;
        }
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (tuple.fields[i]->getType() != columns_[i].type) {
                return false;
            }
        }
        return true;
    }

    std::optional<Tuple> makeTuple(
        const std::vector<std::string>& values) const {
        if (values.size() != columns_.size()) {
            return std::nullopt;
        }

        Tuple tuple;
        for (size_t i = 0; i < columns_.size(); ++i) {
            try {
                Field field = parseLiteralField(columns_[i].type, values[i]);
                tuple.addField(std::make_unique<Field>(std::move(field)));
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
        return tuple;
    }

    std::string digest() const {
        std::ostringstream out;
        out << "schema=[";
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i != 0) {
                out << ",";
            }
            out << columns_[i].name << ":" << fieldTypeName(columns_[i].type);
        }
        out << "]";
        return out.str();
    }

private:
    std::vector<ColumnSchema> columns_;
};

class Table {
public:
    Table() = default;

    Table(std::string name, Schema schema)
        : name_(std::move(name)), schema_(std::move(schema)) {}

    const std::string& name() const {
        return name_;
    }

    const Schema& schema() const {
        return schema_;
    }

    const std::vector<Tuple>& rows() const {
        return rows_;
    }

    bool insert(Tuple tuple) {
        if (!schema_.matches(tuple)) {
            return false;
        }
        rows_.push_back(std::move(tuple));
        return true;
    }

    std::optional<size_t> deleteRows(
        const std::string& column_name,
        const Field& expected_value) {
        std::optional<size_t> column_index = schema_.columnIndex(column_name);
        if (!column_index.has_value()) {
            return std::nullopt;
        }

        size_t before = rows_.size();
        rows_.erase(
            std::remove_if(
                rows_.begin(),
                rows_.end(),
                [&](const Tuple& row) {
                    return *row.fields[*column_index] == expected_value;
                }),
            rows_.end());
        return before - rows_.size();
    }

    std::optional<size_t> updateRows(
        const std::string& set_column,
        const Field& new_value,
        const std::string& where_column,
        const Field& expected_value) {
        std::optional<size_t> set_index = schema_.columnIndex(set_column);
        std::optional<size_t> where_index = schema_.columnIndex(where_column);
        if (!set_index.has_value() || !where_index.has_value()) {
            return std::nullopt;
        }

        size_t updated = 0;
        for (auto& row : rows_) {
            if (*row.fields[*where_index] == expected_value) {
                row.fields[*set_index] = std::make_unique<Field>(new_value);
                ++updated;
            }
        }
        return updated;
    }

    size_t countRows() const {
        return rows_.size();
    }

    std::vector<std::vector<std::string>> selectAllRows() const {
        std::vector<std::vector<std::string>> rows;
        rows.reserve(rows_.size());
        for (const auto& row : rows_) {
            rows.push_back(row.toStrings());
        }
        return rows;
    }

    SelectAllResult selectAll() const {
        return SelectAllResult{schema_.columnNames(), selectAllRows()};
    }

    static Table fromRows(std::string name,
                          Schema schema,
                          const std::vector<std::vector<std::string>>& rows) {
        Table table(std::move(name), std::move(schema));
        for (const auto& row_values : rows) {
            std::optional<Tuple> row = table.schema().makeTuple(row_values);
            if (!row.has_value() || !table.insert(std::move(*row))) {
                throw std::runtime_error("Stored row does not match schema.");
            }
        }
        return table;
    }

    std::string digest() const {
        std::ostringstream out;
        out << name_ << "(" << schema_.digest() << ",rows=[";
        for (size_t r = 0; r < rows_.size(); ++r) {
            if (r != 0) {
                out << ",";
            }
            out << "[";
            auto values = rows_[r].toStrings();
            for (size_t c = 0; c < values.size(); ++c) {
                if (c != 0) {
                    out << ",";
                }
                out << values[c];
            }
            out << "]";
        }
        out << "])";
        return out.str();
    }

private:
    std::string name_;
    Schema schema_;
    std::vector<Tuple> rows_;
};

using TableMap = std::map<std::string, Table>;

using PageID = uint32_t;
using TableId = uint32_t;
constexpr size_t PAGE_SIZE = 4096;
constexpr PageID INVALID_PAGE_ID = UINT32_MAX;
constexpr TableId INVALID_TABLE_ID = 0;
constexpr TableId FIRST_USER_TABLE_ID = 1;

struct StoragePage {
    std::array<char, PAGE_SIZE> bytes{};
};

struct RecordID {
    PageID page_id = INVALID_PAGE_ID;
    uint16_t slot_id = 0;
};

bool operator==(const RecordID& lhs, const RecordID& rhs) {
    return lhs.page_id == rhs.page_id && lhs.slot_id == rhs.slot_id;
}

struct BufferPoolStats {
    size_t page_loads = 0;
    size_t cache_hits = 0;
    size_t evictions = 0;
    size_t page_writes = 0;
};

class StorageManager {
public:
    explicit StorageManager(std::string filename = "buzzdb.dat")
        : filename_(std::move(filename)) {
        openOrCreate();
    }

    ~StorageManager() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    size_t numPages() const {
        return num_pages_;
    }

    const std::string& filename() const {
        return filename_;
    }

    StoragePage load(PageID page_id) {
        if (page_id >= num_pages_) {
            throw std::runtime_error("Page does not exist in storage file.");
        }

        StoragePage page;
        file_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE,
                    std::ios::beg);
        file_.read(page.bytes.data(), PAGE_SIZE);
        if (!file_) {
            throw std::runtime_error("Unable to read page from storage file.");
        }
        return page;
    }

    void flush(PageID page_id, const StoragePage& page) {
        while (page_id >= num_pages_) {
            extend();
        }

        file_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE,
                    std::ios::beg);
        file_.write(page.bytes.data(), PAGE_SIZE);
        file_.flush();
        if (!file_) {
            throw std::runtime_error("Unable to flush page to storage file.");
        }
    }

    PageID extend() {
        StoragePage empty;
        PageID page_id = static_cast<PageID>(num_pages_);
        file_.seekp(0, std::ios::end);
        file_.write(empty.bytes.data(), PAGE_SIZE);
        file_.flush();
        if (!file_) {
            throw std::runtime_error("Unable to extend storage file.");
        }
        ++num_pages_;
        return page_id;
    }

private:
    void openOrCreate() {
        file_.open(filename_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_) {
            file_.clear();
            std::ofstream create(filename_, std::ios::binary);
            if (!create) {
                throw std::runtime_error(
                    "Unable to create storage file: " + filename_);
            }
            create.close();
            file_.open(filename_,
                       std::ios::in | std::ios::out | std::ios::binary);
        }
        if (!file_) {
            throw std::runtime_error(
                "Unable to open storage file: " + filename_);
        }

        file_.seekg(0, std::ios::end);
        std::streamoff size = file_.tellg();
        if (size < 0) {
            throw std::runtime_error(
                "Unable to determine storage file size: " + filename_);
        }
        num_pages_ = static_cast<size_t>(size) / PAGE_SIZE;
    }

    std::string filename_;
    std::fstream file_;
    size_t num_pages_ = 0;
};

class BufferManager {
public:
    explicit BufferManager(std::string filename = "buzzdb.dat",
                           size_t capacity = 8)
        : storage_(std::move(filename)), capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("buffer capacity must be positive");
        }
    }

    bool isEmptyDatabase() const {
        return storage_.numPages() == 0;
    }

    PageID allocatePage() {
        PageID page_id = storage_.extend();
        getPage(page_id);
        markDirty(page_id);
        return page_id;
    }

    StoragePage& fetchPage(PageID page_id) {
        return getPage(page_id);
    }

    void markPageDirty(PageID page_id) {
        markDirty(page_id);
    }

    void flushPage(PageID page_id) {
        auto page = pages_.find(page_id);
        if (page == pages_.end() ||
            dirty_pages_.find(page_id) == dirty_pages_.end()) {
            return;
        }
        storage_.flush(page_id, page->second);
        dirty_pages_.erase(page_id);
        stats_.page_writes++;
    }

    void flushAll() {
        std::vector<PageID> dirty(dirty_pages_.begin(), dirty_pages_.end());
        for (PageID page_id : dirty) {
            auto page = pages_.find(page_id);
            if (page == pages_.end()) {
                continue;
            }
            storage_.flush(page_id, page->second);
            stats_.page_writes++;
            dirty_pages_.erase(page_id);
        }
    }

    size_t pageCount() const {
        return storage_.numPages();
    }

    const BufferPoolStats& stats() const {
        return stats_;
    }

private:
    StoragePage& getPage(PageID page_id) {
        auto found = pages_.find(page_id);
        if (found != pages_.end()) {
            stats_.cache_hits++;
            touch(page_id);
            return found->second;
        }

        if (pages_.size() >= capacity_) {
            evictOne();
        }

        StoragePage page = storage_.load(page_id);
        auto inserted = pages_.emplace(page_id, std::move(page));
        lru_.push_front(page_id);
        lru_positions_[page_id] = lru_.begin();
        stats_.page_loads++;
        return inserted.first->second;
    }

    void markDirty(PageID page_id) {
        dirty_pages_.insert(page_id);
    }

    void touch(PageID page_id) {
        auto position = lru_positions_.find(page_id);
        if (position == lru_positions_.end()) {
            return;
        }
        lru_.erase(position->second);
        lru_.push_front(page_id);
        position->second = lru_.begin();
    }

    void evictOne() {
        if (lru_.empty()) {
            throw std::runtime_error("No page available for eviction.");
        }

        PageID victim = lru_.back();
        lru_.pop_back();
        lru_positions_.erase(victim);

        auto page = pages_.find(victim);
        if (page == pages_.end()) {
            return;
        }
        if (dirty_pages_.find(victim) != dirty_pages_.end()) {
            storage_.flush(victim, page->second);
            dirty_pages_.erase(victim);
            stats_.page_writes++;
        }
        pages_.erase(page);
        stats_.evictions++;
    }

    StorageManager storage_;
    size_t capacity_;
    std::map<PageID, StoragePage> pages_;
    std::list<PageID> lru_;
    std::map<PageID, std::list<PageID>::iterator> lru_positions_;
    std::set<PageID> dirty_pages_;
    BufferPoolStats stats_;
};

void appendU32(std::string& output, uint32_t value) {
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        output.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

uint32_t readU32(const std::string& input, size_t& offset) {
    if (offset + sizeof(uint32_t) > input.size()) {
        throw std::runtime_error("Truncated database image.");
    }
    uint32_t value = 0;
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        value |= (static_cast<uint32_t>(
                      static_cast<unsigned char>(input[offset + i]))
                  << (8 * i));
    }
    offset += sizeof(uint32_t);
    return value;
}

void appendString(std::string& output, const std::string& value) {
    appendU32(output, static_cast<uint32_t>(value.size()));
    output.append(value);
}

std::string readString(const std::string& input, size_t& offset) {
    uint32_t size = readU32(input, offset);
    if (offset + size > input.size()) {
        throw std::runtime_error("Truncated string in database image.");
    }
    std::string value = input.substr(offset, size);
    offset += size;
    return value;
}

uint16_t readPageU16(const StoragePage& page, size_t offset) {
    return static_cast<uint16_t>(
        static_cast<unsigned char>(page.bytes[offset]) |
        (static_cast<unsigned char>(page.bytes[offset + 1]) << 8));
}

void writePageU16(StoragePage& page, size_t offset, uint16_t value) {
    page.bytes[offset] = static_cast<char>(value & 0xff);
    page.bytes[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

uint32_t readPageU32(const StoragePage& page, size_t offset) {
    uint32_t value = 0;
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        value |= (static_cast<uint32_t>(
                      static_cast<unsigned char>(page.bytes[offset + i]))
                  << (8 * i));
    }
    return value;
}

void writePageU32(StoragePage& page, size_t offset, uint32_t value) {
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        page.bytes[offset + i] =
            static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

enum class PageType : uint16_t {
    EMPTY = 0,
    TABLE_HEAP = 1
};

class SlottedPage {
public:
    SlottedPage(PageID page_id, StoragePage& page)
        : page_id_(page_id), page_(page) {}

    void initialize(TableId table_id) {
        page_.bytes.fill(0);
        const auto& magic = pageMagic();
        std::copy(magic.begin(), magic.end(), page_.bytes.begin());
        writePageU16(page_, TYPE_OFFSET,
                     static_cast<uint16_t>(PageType::TABLE_HEAP));
        writePageU32(page_, TABLE_ID_OFFSET, table_id);
        writePageU32(page_, NEXT_PAGE_OFFSET, INVALID_PAGE_ID);
        writePageU16(page_, SLOT_COUNT_OFFSET, 0);
        writePageU16(page_, FREE_END_OFFSET, PAGE_SIZE);
    }

    bool isTablePageFor(TableId table_id) const {
        const auto& magic = pageMagic();
        return std::equal(magic.begin(), magic.end(), page_.bytes.begin()) &&
               static_cast<PageType>(readPageU16(page_, TYPE_OFFSET)) ==
                   PageType::TABLE_HEAP &&
               readPageU32(page_, TABLE_ID_OFFSET) == table_id;
    }

    std::optional<RecordID> addRecord(const std::string& record) {
        if (record.size() > UINT16_MAX) {
            return std::nullopt;
        }
        uint16_t slot_count = readPageU16(page_, SLOT_COUNT_OFFSET);
        size_t new_slot_offset = slotOffset(slot_count);
        uint16_t free_end = readPageU16(page_, FREE_END_OFFSET);
        if (new_slot_offset + SLOT_SIZE > free_end ||
            record.size() > free_end - new_slot_offset - SLOT_SIZE) {
            return std::nullopt;
        }

        free_end = static_cast<uint16_t>(free_end - record.size());
        std::memcpy(page_.bytes.data() + free_end,
                    record.data(),
                    record.size());
        writeSlot(slot_count,
                  free_end,
                  static_cast<uint16_t>(record.size()),
                  true);
        writePageU16(page_, SLOT_COUNT_OFFSET, slot_count + 1);
        writePageU16(page_, FREE_END_OFFSET, free_end);
        return RecordID{page_id_, slot_count};
    }

    std::optional<std::string> readRecord(uint16_t slot_id) const {
        if (slot_id >= readPageU16(page_, SLOT_COUNT_OFFSET)) {
            return std::nullopt;
        }
        Slot slot = readSlot(slot_id);
        if (!slot.occupied) {
            return std::nullopt;
        }
        return std::string(page_.bytes.data() + slot.offset, slot.length);
    }

    bool deleteRecord(uint16_t slot_id) {
        if (slot_id >= readPageU16(page_, SLOT_COUNT_OFFSET)) {
            return false;
        }
        Slot slot = readSlot(slot_id);
        if (!slot.occupied) {
            return false;
        }
        writeSlot(slot_id, slot.offset, slot.length, false);
        return true;
    }

    bool updateRecord(uint16_t slot_id, const std::string& record) {
        if (slot_id >= readPageU16(page_, SLOT_COUNT_OFFSET)) {
            return false;
        }
        Slot slot = readSlot(slot_id);
        if (!slot.occupied || record.size() > slot.length) {
            return false;
        }
        std::memcpy(page_.bytes.data() + slot.offset,
                    record.data(),
                    record.size());
        writeSlot(slot_id,
                  slot.offset,
                  static_cast<uint16_t>(record.size()),
                  true);
        return true;
    }

    std::vector<std::pair<RecordID, std::string>> records() const {
        std::vector<std::pair<RecordID, std::string>> output;
        uint16_t slot_count = readPageU16(page_, SLOT_COUNT_OFFSET);
        for (uint16_t slot_id = 0; slot_id < slot_count; ++slot_id) {
            std::optional<std::string> record = readRecord(slot_id);
            if (record.has_value()) {
                output.push_back({RecordID{page_id_, slot_id}, *record});
            }
        }
        return output;
    }

private:
    struct Slot {
        uint16_t offset;
        uint16_t length;
        bool occupied;
    };

    static constexpr size_t TYPE_OFFSET = 4;
    static constexpr size_t TABLE_ID_OFFSET = 8;
    static constexpr size_t NEXT_PAGE_OFFSET = 12;
    static constexpr size_t SLOT_COUNT_OFFSET = 16;
    static constexpr size_t FREE_END_OFFSET = 18;
    static constexpr size_t HEADER_SIZE = 20;
    static constexpr size_t SLOT_SIZE = 5;

    static const std::array<char, 4>& pageMagic() {
        static const std::array<char, 4> magic{'B', 'P', 'G', '1'};
        return magic;
    }

    static size_t slotOffset(uint16_t slot_id) {
        return HEADER_SIZE + static_cast<size_t>(slot_id) * SLOT_SIZE;
    }

    Slot readSlot(uint16_t slot_id) const {
        size_t offset = slotOffset(slot_id);
        return Slot{
            readPageU16(page_, offset),
            readPageU16(page_, offset + 2),
            page_.bytes[offset + 4] != 0
        };
    }

    void writeSlot(uint16_t slot_id,
                   uint16_t offset,
                   uint16_t length,
                   bool occupied) {
        size_t slot_offset = slotOffset(slot_id);
        writePageU16(page_, slot_offset, offset);
        writePageU16(page_, slot_offset + 2, length);
        page_.bytes[slot_offset + 4] = occupied ? 1 : 0;
    }

    PageID page_id_;
    StoragePage& page_;
};

std::string serializeTupleRecord(const Tuple& tuple) {
    std::string output;
    std::vector<std::string> values = tuple.toStrings();
    appendU32(output, static_cast<uint32_t>(values.size()));
    for (const auto& value : values) {
        appendString(output, value);
    }
    return output;
}

Tuple deserializeTupleRecord(const Schema& schema, const std::string& record) {
    size_t offset = 0;
    uint32_t value_count = readU32(record, offset);
    std::vector<std::string> values;
    values.reserve(value_count);
    for (uint32_t i = 0; i < value_count; ++i) {
        values.push_back(readString(record, offset));
    }
    if (offset != record.size()) {
        throw std::runtime_error("Trailing bytes in tuple record.");
    }
    std::optional<Tuple> tuple = schema.makeTuple(values);
    if (!tuple.has_value()) {
        throw std::runtime_error("Stored tuple does not match schema.");
    }
    return std::move(*tuple);
}

struct TableMetadata {
    TableId table_id = INVALID_TABLE_ID;
    std::string name;
    Schema schema;
    std::vector<PageID> page_ids;
    PageID first_page = INVALID_PAGE_ID;
    PageID last_page = INVALID_PAGE_ID;
    size_t row_count = 0;
};

class Catalog {
public:
    explicit Catalog(BufferManager& buffer_manager)
        : buffer_manager_(buffer_manager) {}

    void bootstrap() {
        if (buffer_manager_.isEmptyDatabase()) {
            PageID page_id = buffer_manager_.allocatePage();
            if (page_id != 0) {
                throw std::runtime_error("Catalog must start at page 0.");
            }
            initialized_new_database_ = true;
            persist();
            return;
        }
        load();
    }

    bool isNewDatabase() const {
        return initialized_new_database_;
    }

    bool hasTable(const std::string& name) const {
        return tables_.find(name) != tables_.end();
    }

    TableMetadata& createTable(const std::string& name, Schema schema) {
        if (hasTable(name)) {
            throw std::runtime_error("Table already exists: " + name);
        }

        TableId table_id = next_table_id_++;
        PageID first_page = allocateHeapPage(table_id);
        TableMetadata metadata{
            table_id,
            name,
            std::move(schema),
            {first_page},
            first_page,
            first_page,
            0
        };
        auto inserted = tables_.emplace(name, std::move(metadata));
        table_names_by_id_[table_id] = name;
        persist();
        return inserted.first->second;
    }

    TableMetadata& getTable(const std::string& name) {
        auto found = tables_.find(name);
        if (found == tables_.end()) {
            throw std::runtime_error("Unknown table: " + name);
        }
        return found->second;
    }

    const TableMetadata& getTable(const std::string& name) const {
        auto found = tables_.find(name);
        if (found == tables_.end()) {
            throw std::runtime_error("Unknown table: " + name);
        }
        return found->second;
    }

    std::map<std::string, TableMetadata>& mutableTables() {
        return tables_;
    }

    const std::map<std::string, TableMetadata>& tables() const {
        return tables_;
    }

    PageID allocateHeapPage(TableId table_id) {
        PageID page_id = buffer_manager_.allocatePage();
        StoragePage& storage_page = buffer_manager_.fetchPage(page_id);
        SlottedPage page(page_id, storage_page);
        page.initialize(table_id);
        buffer_manager_.markPageDirty(page_id);
        buffer_manager_.flushPage(page_id);
        return page_id;
    }

    void persistTableMetadata() {
        persist();
    }

private:
    static const std::array<char, 8>& catalogMagic() {
        static const std::array<char, 8> magic{
            'B', 'C', 'A', 'T', '1', '0', '9', '\0'
        };
        return magic;
    }

    std::string serialize() const {
        std::string output;
        output.append("CATALOG1");
        appendU32(output, next_table_id_);
        appendU32(output, static_cast<uint32_t>(tables_.size()));
        for (const auto& entry : tables_) {
            const TableMetadata& metadata = entry.second;
            appendU32(output, metadata.table_id);
            appendString(output, metadata.name);
            appendU32(output,
                      static_cast<uint32_t>(metadata.schema.columns().size()));
            for (const auto& column : metadata.schema.columns()) {
                appendString(output, column.name);
                appendU32(output, static_cast<uint32_t>(column.type));
            }
            appendU32(output, static_cast<uint32_t>(metadata.row_count));
            appendU32(output, static_cast<uint32_t>(metadata.page_ids.size()));
            for (PageID page_id : metadata.page_ids) {
                appendU32(output, page_id);
            }
            appendU32(output, metadata.first_page);
            appendU32(output, metadata.last_page);
        }
        return output;
    }

    void deserialize(const std::string& image) {
        tables_.clear();
        table_names_by_id_.clear();
        const std::string magic = "CATALOG1";
        if (image.size() < magic.size() ||
            image.compare(0, magic.size(), magic) != 0) {
            throw std::runtime_error("Invalid catalog image.");
        }
        size_t offset = magic.size();
        next_table_id_ = readU32(image, offset);
        uint32_t table_count = readU32(image, offset);
        for (uint32_t table_i = 0; table_i < table_count; ++table_i) {
            TableMetadata metadata;
            metadata.table_id = readU32(image, offset);
            metadata.name = readString(image, offset);
            uint32_t column_count = readU32(image, offset);
            std::vector<ColumnSchema> columns;
            columns.reserve(column_count);
            for (uint32_t column_i = 0; column_i < column_count; ++column_i) {
                std::string name = readString(image, offset);
                uint32_t type = readU32(image, offset);
                if (type > static_cast<uint32_t>(STRING)) {
                    throw std::runtime_error("Invalid column type in catalog.");
                }
                columns.push_back(
                    ColumnSchema{name, static_cast<FieldType>(type)});
            }
            metadata.schema = Schema(std::move(columns));
            metadata.row_count = readU32(image, offset);
            uint32_t page_count = readU32(image, offset);
            for (uint32_t page_i = 0; page_i < page_count; ++page_i) {
                metadata.page_ids.push_back(readU32(image, offset));
            }
            metadata.first_page = readU32(image, offset);
            metadata.last_page = readU32(image, offset);
            table_names_by_id_[metadata.table_id] = metadata.name;
            tables_.emplace(metadata.name, std::move(metadata));
        }
        if (offset != image.size()) {
            throw std::runtime_error("Trailing bytes in catalog image.");
        }
    }

    void persist() {
        constexpr size_t header_size = 12;
        std::string image = serialize();
        if (image.size() > PAGE_SIZE - header_size) {
            throw std::runtime_error("Catalog page is full.");
        }
        StoragePage& page = buffer_manager_.fetchPage(0);
        page.bytes.fill(0);
        const auto& magic = catalogMagic();
        std::copy(magic.begin(), magic.end(), page.bytes.begin());
        writePageU32(page, 8, static_cast<uint32_t>(image.size()));
        std::memcpy(page.bytes.data() + header_size,
                    image.data(),
                    image.size());
        buffer_manager_.markPageDirty(0);
        buffer_manager_.flushPage(0);
    }

    void load() {
        constexpr size_t header_size = 12;
        StoragePage& page = buffer_manager_.fetchPage(0);
        const auto& magic = catalogMagic();
        if (!std::equal(magic.begin(), magic.end(), page.bytes.begin())) {
            throw std::runtime_error("buzzdb.dat is missing a catalog page.");
        }
        uint32_t image_size = readPageU32(page, 8);
        if (image_size > PAGE_SIZE - header_size) {
            throw std::runtime_error("Catalog image is too large.");
        }
        deserialize(std::string(page.bytes.data() + header_size, image_size));
    }

    BufferManager& buffer_manager_;
    TableId next_table_id_ = FIRST_USER_TABLE_ID;
    bool initialized_new_database_ = false;
    std::map<std::string, TableMetadata> tables_;
    std::map<TableId, std::string> table_names_by_id_;
};

class TableHeap {
public:
    TableHeap(TableMetadata& metadata, BufferManager& buffer_manager)
        : metadata_(metadata), buffer_manager_(buffer_manager) {}

    std::optional<RecordID> insert(Tuple tuple) {
        std::string record = serializeTupleRecord(tuple);
        if (metadata_.last_page != INVALID_PAGE_ID) {
            std::optional<RecordID> inserted =
                insertIntoPage(metadata_.last_page, record);
            if (inserted.has_value()) {
                metadata_.row_count++;
                return inserted;
            }
        }

        PageID page_id = allocatePage();
        std::optional<RecordID> inserted = insertIntoPage(page_id, record);
        if (!inserted.has_value()) {
            throw std::runtime_error("Tuple is too large for a heap page.");
        }
        metadata_.row_count++;
        return inserted;
    }

    std::vector<std::pair<RecordID, Tuple>> readAllWithRecordIDs() {
        std::vector<std::pair<RecordID, Tuple>> rows;
        for (PageID page_id : metadata_.page_ids) {
            SlottedPage page = heapPage(page_id);
            for (const auto& record : page.records()) {
                rows.push_back({
                    record.first,
                    deserializeTupleRecord(metadata_.schema, record.second)
                });
            }
        }
        return rows;
    }

    std::vector<Tuple> readAllTuples() {
        std::vector<Tuple> rows;
        for (auto& row : readAllWithRecordIDs()) {
            rows.push_back(std::move(row.second));
        }
        return rows;
    }

    std::vector<Tuple> readByRecordIDs(const std::vector<RecordID>& record_ids) {
        std::vector<Tuple> rows;
        for (const auto& record_id : record_ids) {
            if (record_id.page_id == INVALID_PAGE_ID) {
                continue;
            }
            SlottedPage page = heapPage(record_id.page_id);
            std::optional<std::string> record =
                page.readRecord(record_id.slot_id);
            if (record.has_value()) {
                rows.push_back(
                    deserializeTupleRecord(metadata_.schema, *record));
            }
        }
        return rows;
    }

    size_t deleteRows(const std::string& column, const Field& expected_value) {
        std::optional<size_t> column_index =
            metadata_.schema.columnIndex(column);
        if (!column_index.has_value()) {
            throw std::runtime_error("Unknown column: " + column);
        }

        size_t deleted = 0;
        for (PageID page_id : metadata_.page_ids) {
            SlottedPage page = heapPage(page_id);
            for (const auto& record : page.records()) {
                Tuple tuple =
                    deserializeTupleRecord(metadata_.schema, record.second);
                if (*tuple.fields[*column_index] == expected_value &&
                    page.deleteRecord(record.first.slot_id)) {
                    buffer_manager_.markPageDirty(page_id);
                    deleted++;
                }
            }
            buffer_manager_.flushPage(page_id);
        }
        metadata_.row_count -= deleted;
        return deleted;
    }

    size_t updateRows(const std::string& set_column,
                      const Field& new_value,
                      const std::string& where_column,
                      const Field& expected_value) {
        std::optional<size_t> set_index =
            metadata_.schema.columnIndex(set_column);
        std::optional<size_t> where_index =
            metadata_.schema.columnIndex(where_column);
        if (!set_index.has_value() || !where_index.has_value()) {
            throw std::runtime_error("Unknown column in update.");
        }

        std::vector<std::pair<RecordID, Tuple>> rows = readAllWithRecordIDs();
        size_t updated = 0;
        for (auto& row : rows) {
            if (!(*row.second.fields[*where_index] == expected_value)) {
                continue;
            }
            row.second.fields[*set_index] = std::make_unique<Field>(new_value);
            std::string new_record = serializeTupleRecord(row.second);
            SlottedPage page = heapPage(row.first.page_id);
            if (page.updateRecord(row.first.slot_id, new_record)) {
                buffer_manager_.markPageDirty(row.first.page_id);
                buffer_manager_.flushPage(row.first.page_id);
            } else if (page.deleteRecord(row.first.slot_id)) {
                buffer_manager_.markPageDirty(row.first.page_id);
                buffer_manager_.flushPage(row.first.page_id);
                metadata_.row_count--;
                insert(row.second);
            }
            updated++;
        }
        return updated;
    }

    Table toTable() {
        std::vector<std::vector<std::string>> rows;
        for (const auto& tuple : readAllTuples()) {
            rows.push_back(tuple.toStrings());
        }
        return Table::fromRows(metadata_.name, metadata_.schema, rows);
    }

private:
    SlottedPage heapPage(PageID page_id) {
        StoragePage& storage_page = buffer_manager_.fetchPage(page_id);
        SlottedPage page(page_id, storage_page);
        if (!page.isTablePageFor(metadata_.table_id)) {
            throw std::runtime_error(
                "Page does not belong to table: " + metadata_.name);
        }
        return page;
    }

    std::optional<RecordID> insertIntoPage(PageID page_id,
                                           const std::string& record) {
        SlottedPage page = heapPage(page_id);
        std::optional<RecordID> inserted = page.addRecord(record);
        if (inserted.has_value()) {
            buffer_manager_.markPageDirty(page_id);
            buffer_manager_.flushPage(page_id);
        }
        return inserted;
    }

    PageID allocatePage() {
        PageID page_id = buffer_manager_.allocatePage();
        StoragePage& storage_page = buffer_manager_.fetchPage(page_id);
        SlottedPage page(page_id, storage_page);
        page.initialize(metadata_.table_id);
        buffer_manager_.markPageDirty(page_id);
        buffer_manager_.flushPage(page_id);
        metadata_.page_ids.push_back(page_id);
        if (metadata_.first_page == INVALID_PAGE_ID) {
            metadata_.first_page = page_id;
        }
        metadata_.last_page = page_id;
        return page_id;
    }

    TableMetadata& metadata_;
    BufferManager& buffer_manager_;
};

TableMap loadTablesFromCatalog(Catalog& catalog, BufferManager& buffer_manager) {
    TableMap tables;
    for (auto& entry : catalog.mutableTables()) {
        TableHeap heap(entry.second, buffer_manager);
        tables.emplace(entry.first, heap.toTable());
    }
    return tables;
}

std::vector<std::vector<std::string>> tupleRowsToStrings(
    const std::vector<Tuple>& tuples) {
    std::vector<std::vector<std::string>> rows;
    rows.reserve(tuples.size());
    for (const auto& tuple : tuples) {
        rows.push_back(tuple.toStrings());
    }
    return rows;
}

class HashIndex {
public:
    void rebuild(const TableMap& tables) {
        entries_.clear();
        for (const auto& table_entry : tables) {
            const Table& table = table_entry.second;
            std::vector<std::string> column_names =
                table.schema().columnNames();
            const auto& rows = table.rows();
            for (size_t row_id = 0; row_id < rows.size(); ++row_id) {
                indexTuple(table.name(),
                           column_names,
                           rows[row_id],
                           RecordID{0, static_cast<uint16_t>(row_id)});
            }
        }
    }

    void rebuild(Catalog& catalog, BufferManager& buffer_manager) {
        entries_.clear();
        for (auto& table_entry : catalog.mutableTables()) {
            TableHeap heap(table_entry.second, buffer_manager);
            std::vector<std::string> column_names =
                table_entry.second.schema.columnNames();
            for (const auto& row : heap.readAllWithRecordIDs()) {
                indexTuple(table_entry.second.name,
                           column_names,
                           row.second,
                           row.first);
            }
        }
    }

    std::vector<RecordID> lookupRecords(const std::string& table,
                                        const std::string& column,
                                        const std::string& value) const {
        auto table_entry = entries_.find(table);
        if (table_entry == entries_.end()) {
            return {};
        }
        auto column_entry = table_entry->second.find(column);
        if (column_entry == table_entry->second.end()) {
            return {};
        }
        auto value_entry = column_entry->second.find(value);
        if (value_entry == column_entry->second.end()) {
            return {};
        }
        return value_entry->second;
    }

    std::vector<size_t> lookup(const std::string& table,
                               const std::string& column,
                               const std::string& value) const {
        std::vector<size_t> slots;
        for (const auto& record : lookupRecords(table, column, value)) {
            slots.push_back(record.slot_id);
        }
        return slots;
    }

    std::string digest() const {
        size_t keys = 0;
        size_t records = 0;
        for (const auto& table_entry : entries_) {
            for (const auto& column_entry : table_entry.second) {
                keys += column_entry.second.size();
                for (const auto& value_entry : column_entry.second) {
                    records += value_entry.second.size();
                }
            }
        }
        return "HashIndex(keys=" + std::to_string(keys) +
               ",records=" + std::to_string(records) + ")";
    }

private:
    void indexTuple(const std::string& table,
                    const std::vector<std::string>& column_names,
                    const Tuple& tuple,
                    RecordID record_id) {
        std::vector<std::string> values = tuple.toStrings();
        for (size_t column_i = 0;
             column_i < column_names.size() && column_i < values.size();
             ++column_i) {
            entries_[table][column_names[column_i]][values[column_i]]
                .push_back(record_id);
        }
    }

    std::map<
        std::string,
        std::map<std::string, std::map<std::string, std::vector<RecordID>>>>
        entries_;
};

class BuzzDBOperator {
public:
    virtual ~BuzzDBOperator() = default;
    virtual Result execute() = 0;
};

class CreateTableOperator : public BuzzDBOperator {
public:
    CreateTableOperator(TableMap& tables, CreateTableCommand command)
        : tables_(tables), command_(std::move(command)) {}

    Result execute() override {
        if (tables_.find(command_.table) != tables_.end()) {
            return TableAlreadyExistsResult{};
        }
        try {
            tables_.emplace(
                command_.table,
                Table{command_.table, Schema::fromColumnSpecs(command_.columns)});
        } catch (const std::exception&) {
            return InvalidSchemaResult{};
        }
        return CreateTableOkResult{};
    }

private:
    TableMap& tables_;
    CreateTableCommand command_;
};

class InsertOperator : public BuzzDBOperator {
public:
    InsertOperator(TableMap& tables, InsertRowCommand command)
        : tables_(tables), command_(std::move(command)) {}

    Result execute() override {
        auto table = tables_.find(command_.table);
        if (table == tables_.end()) {
            return TableNotFoundResult{};
        }

        std::optional<Tuple> row =
            table->second.schema().makeTuple(command_.values);
        if (!row.has_value()) {
            return SchemaMismatchResult{};
        }
        if (!table->second.insert(std::move(*row))) {
            return SchemaMismatchResult{};
        }
        return InsertOkResult{};
    }

private:
    TableMap& tables_;
    InsertRowCommand command_;
};

class DeleteOperator : public BuzzDBOperator {
public:
    DeleteOperator(TableMap& tables, DeleteRowsCommand command)
        : tables_(tables), command_(std::move(command)) {}

    Result execute() override {
        auto table = tables_.find(command_.table);
        if (table == tables_.end()) {
            return TableNotFoundResult{};
        }

        std::optional<FieldType> column_type =
            table->second.schema().columnType(command_.column);
        if (!column_type.has_value()) {
            return SchemaMismatchResult{};
        }

        Field expected = Field("");
        try {
            expected = parseLiteralField(*column_type, command_.value);
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }

        std::optional<size_t> deleted =
            table->second.deleteRows(command_.column, expected);
        if (!deleted.has_value()) {
            return SchemaMismatchResult{};
        }
        return DeleteRowsResult{*deleted};
    }

private:
    TableMap& tables_;
    DeleteRowsCommand command_;
};

class UpdateOperator : public BuzzDBOperator {
public:
    UpdateOperator(TableMap& tables, UpdateRowsCommand command)
        : tables_(tables), command_(std::move(command)) {}

    Result execute() override {
        auto table = tables_.find(command_.table);
        if (table == tables_.end()) {
            return TableNotFoundResult{};
        }

        std::optional<FieldType> set_type =
            table->second.schema().columnType(command_.set_column);
        std::optional<FieldType> where_type =
            table->second.schema().columnType(command_.where_column);
        if (!set_type.has_value() || !where_type.has_value()) {
            return SchemaMismatchResult{};
        }

        Field new_value = Field("");
        Field expected = Field("");
        try {
            new_value = parseLiteralField(*set_type, command_.set_value);
            expected = parseLiteralField(*where_type, command_.where_value);
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }

        std::optional<size_t> updated =
            table->second.updateRows(
                command_.set_column,
                new_value,
                command_.where_column,
                expected);
        if (!updated.has_value()) {
            return SchemaMismatchResult{};
        }
        return UpdateRowsResult{*updated};
    }

private:
    TableMap& tables_;
    UpdateRowsCommand command_;
};

class ScanOperator : public BuzzDBOperator {
public:
    ScanOperator(const TableMap& tables, SelectAllCommand command)
        : tables_(tables), command_(std::move(command)) {}

    Result execute() override {
        auto table = tables_.find(command_.table);
        if (table == tables_.end()) {
            return TableNotFoundResult{};
        }
        return table->second.selectAll();
    }

private:
    const TableMap& tables_;
    SelectAllCommand command_;
};

class IndexLookupOperator : public BuzzDBOperator {
public:
    IndexLookupOperator(const TableMap& tables,
                        const HashIndex& index,
                        SelectWhereCommand command)
        : tables_(tables), index_(index), command_(std::move(command)) {}

    Result execute() override {
        auto table = tables_.find(command_.table);
        if (table == tables_.end()) {
            return TableNotFoundResult{};
        }

        std::optional<FieldType> column_type =
            table->second.schema().columnType(command_.column);
        if (!column_type.has_value()) {
            return SchemaMismatchResult{};
        }

        std::string canonical_value;
        try {
            canonical_value =
                parseLiteralField(*column_type, command_.value).toString();
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }

        std::vector<std::vector<std::string>> rows;
        for (const auto& record :
             index_.lookupRecords(command_.table,
                                  command_.column,
                                  canonical_value)) {
            if (record.slot_id < table->second.rows().size()) {
                rows.push_back(
                    table->second.rows()[record.slot_id].toStrings());
            }
        }
        return SelectAllResult{table->second.schema().columnNames(), rows};
    }

private:
    const TableMap& tables_;
    const HashIndex& index_;
    SelectWhereCommand command_;
};

class CountOperator : public BuzzDBOperator {
public:
    CountOperator(const TableMap& tables, CountRowsCommand command)
        : tables_(tables), command_(std::move(command)) {}

    Result execute() override {
        auto table = tables_.find(command_.table);
        if (table == tables_.end()) {
            return TableNotFoundResult{};
        }
        return CountRowsResult{table->second.countRows()};
    }

private:
    const TableMap& tables_;
    CountRowsCommand command_;
};

struct QueryComponents {
    bool select_all = false;
    std::string table_name;
    std::optional<std::string> where_column;
    std::optional<std::string> where_value;
};

bool isIdentifier(const std::string& token) {
    static const std::regex identifier(
        "^[A-Za-z_][A-Za-z0-9_]*$");
    return std::regex_match(token, identifier);
}

QueryComponents parseQuery(const std::string& query) {
    if (!std::regex_search(
            query,
            std::regex("^\\s*(PROJECT|SELECT)\\s+",
                       std::regex_constants::icase))) {
        throw std::runtime_error("Query must start with PROJECT or SELECT.");
    }

    std::regex selectStarRegex(
        "^\\s*(PROJECT|SELECT)\\s+\\*\\s+FROM\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)"
        "(?:\\s+WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+))?"
        "\\s*;?\\s*$",
        std::regex_constants::icase);
    std::smatch matches;
    if (!std::regex_match(query, matches, selectStarRegex)) {
        throw std::runtime_error(
            "v109 supports SELECT * FROM <table> queries.");
    }

    QueryComponents components;
    components.select_all = true;
    components.table_name = matches[2];
    if (!isIdentifier(components.table_name)) {
        throw std::runtime_error("Query must name a valid table.");
    }
    if (matches[3].matched) {
        components.where_column = matches[3];
        components.where_value = matches[4];
        if (!isIdentifier(*components.where_column)) {
            throw std::runtime_error("Query must name a valid column.");
        }
    }
    return components;
}

Command parseSQL(const std::string& sql) {
    std::string statement = stripOptionalSemicolon(sql);
    if (std::regex_search(
            statement,
            std::regex("^\\s*(PROJECT|SELECT)\\s+",
                       std::regex_constants::icase))) {
        QueryComponents components = parseQuery(statement);
        if (components.select_all) {
            if (components.where_column.has_value()) {
                return SelectWhereCommand{
                    components.table_name,
                    *components.where_column,
                    *components.where_value
                };
            }
            return SelectAllCommand{components.table_name};
        }
        throw std::runtime_error("Unsupported query.");
    }

    std::smatch matches;
    std::regex createRegex(
        "^\\s*CREATE\\s+TABLE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*)\\)\\s*$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, createRegex)) {
        return CreateTableCommand{matches[1], splitCommaList(matches[2])};
    }

    std::regex insertRegex(
        "^\\s*INSERT\\s+([A-Za-z_][A-Za-z0-9_]*)\\|(.*)$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, insertRegex)) {
        return InsertRowCommand{matches[1], splitPipeLine(matches[2])};
    }

    std::regex updateRegex(
        "^\\s*UPDATE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+SET\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s+WHERE\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s*$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, updateRegex)) {
        return UpdateRowsCommand{
            matches[1],
            matches[2],
            matches[3],
            matches[4],
            matches[5]
        };
    }

    std::regex deleteRegex(
        "^\\s*DELETE\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+WHERE\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s*$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, deleteRegex)) {
        return DeleteRowsCommand{matches[1], matches[2], matches[3]};
    }

    throw std::runtime_error("Unsupported SQL command: " + sql);
}

std::vector<Command> loadTupleFileCommands(std::istream& input,
                                           const std::string& source_name) {
    std::vector<Command> commands;
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trimCopy(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.find('|') == std::string::npos) {
            throw std::runtime_error(
                "Malformed tuple row in " + source_name +
                " at line " + std::to_string(line_number));
        }
        commands.push_back(parseSQL("INSERT " + line));
    }
    return commands;
}

std::vector<Command> loadTupleFileCommands(const std::string& filename) {
    std::ifstream input(filename);
    if (!input) {
        throw std::runtime_error("Unable to open tuple file: " + filename);
    }
    return loadTupleFileCommands(input, filename);
}

class BuzzDBCore {
public:
    BuzzDBCore() {
        index_.rebuild(tables_);
    }

    explicit BuzzDBCore(std::string database_filename)
        : database_filename_(std::move(database_filename)),
          buffer_manager_(
              std::make_unique<BufferManager>(*database_filename_)),
          catalog_(std::make_unique<Catalog>(*buffer_manager_)) {
        catalog_->bootstrap();
        reloadDurableCaches();
    }

    BuzzDBCore(const BuzzDBCore& other)
        : tables_(other.tables_), index_(other.index_) {}

    BuzzDBCore& operator=(const BuzzDBCore& other) {
        if (this == &other) {
            return *this;
        }
        tables_ = other.tables_;
        index_ = other.index_;
        database_filename_.reset();
        buffer_manager_.reset();
        catalog_.reset();
        return *this;
    }

    Result execute(const Command& command) {
        if (durable()) {
            return executeDurable(command);
        }

        bool command_may_mutate =
            std::holds_alternative<CreateTableCommand>(command) ||
            std::holds_alternative<InsertRowCommand>(command) ||
            std::holds_alternative<DeleteRowsCommand>(command) ||
            std::holds_alternative<UpdateRowsCommand>(command);

        Result result = TableNotFoundResult{};
        if (const auto* create = std::get_if<CreateTableCommand>(&command)) {
            result = CreateTableOperator(tables_, *create).execute();
        } else if (const auto* insert = std::get_if<InsertRowCommand>(&command)) {
            result = InsertOperator(tables_, *insert).execute();
        } else if (const auto* delete_rows =
                       std::get_if<DeleteRowsCommand>(&command)) {
            result = DeleteOperator(tables_, *delete_rows).execute();
        } else if (const auto* update_rows =
                       std::get_if<UpdateRowsCommand>(&command)) {
            result = UpdateOperator(tables_, *update_rows).execute();
        } else if (const auto* select =
                       std::get_if<SelectAllCommand>(&command)) {
            result = ScanOperator(tables_, *select).execute();
        } else if (const auto* lookup =
                       std::get_if<SelectWhereCommand>(&command)) {
            result = IndexLookupOperator(tables_, index_, *lookup).execute();
        } else if (const auto* count = std::get_if<CountRowsCommand>(&command)) {
            result = CountOperator(tables_, *count).execute();
        } else {
            throw std::runtime_error(
                "BuzzDBCore received a non-database command.");
        }

        if (command_may_mutate && mutationSucceeded(result)) {
            index_.rebuild(tables_);
        }
        return result;
    }

    std::vector<Result> executeAll(const std::vector<Command>& commands) {
        std::vector<Result> results;
        results.reserve(commands.size());
        for (const auto& command : commands) {
            results.push_back(execute(command));
        }
        return results;
    }

    std::string digest() const {
        std::ostringstream out;
        out << "BuzzDB(tables=" << tables_.size();
        for (const auto& entry : tables_) {
            out << ";table=" << entry.second.digest();
        }
        out << ";index=" << index_.digest();
        out << ")";
        return out.str();
    }

    std::vector<size_t> lookupIndex(const std::string& table,
                                    const std::string& column,
                                    const std::string& value) const {
        return index_.lookup(table, column, value);
    }

    bool durable() const {
        return buffer_manager_ != nullptr;
    }

    bool isNewDatabase() const {
        if (catalog_) {
            return catalog_->isNewDatabase();
        }
        return tables_.empty();
    }

    size_t storagePageCount() const {
        return buffer_manager_ ? buffer_manager_->pageCount() : 0;
    }

    BufferPoolStats bufferStats() const {
        return buffer_manager_ ? buffer_manager_->stats() : BufferPoolStats{};
    }

private:
    static bool mutationSucceeded(const Result& result) {
        return std::holds_alternative<CreateTableOkResult>(result) ||
               std::holds_alternative<InsertOkResult>(result) ||
               std::holds_alternative<DeleteRowsResult>(result) ||
               std::holds_alternative<UpdateRowsResult>(result);
    }

    Result executeDurable(const Command& command) {
        if (const auto* create = std::get_if<CreateTableCommand>(&command)) {
            return durableCreate(*create);
        }
        if (const auto* insert = std::get_if<InsertRowCommand>(&command)) {
            return durableInsert(*insert);
        }
        if (const auto* delete_rows =
                std::get_if<DeleteRowsCommand>(&command)) {
            return durableDelete(*delete_rows);
        }
        if (const auto* update_rows =
                std::get_if<UpdateRowsCommand>(&command)) {
            return durableUpdate(*update_rows);
        }
        if (const auto* select = std::get_if<SelectAllCommand>(&command)) {
            return durableSelectAll(*select);
        }
        if (const auto* lookup = std::get_if<SelectWhereCommand>(&command)) {
            return durableSelectWhere(*lookup);
        }
        if (const auto* count = std::get_if<CountRowsCommand>(&command)) {
            return durableCount(*count);
        }
        throw std::runtime_error("BuzzDBCore received a non-database command.");
    }

    Result durableCreate(const CreateTableCommand& command) {
        if (catalog_->hasTable(command.table)) {
            return TableAlreadyExistsResult{};
        }
        try {
            catalog_->createTable(
                command.table,
                Schema::fromColumnSpecs(command.columns));
        } catch (const std::exception&) {
            return InvalidSchemaResult{};
        }
        reloadDurableCaches();
        return CreateTableOkResult{};
    }

    Result durableInsert(const InsertRowCommand& command) {
        TableMetadata* metadata = findDurableTable(command.table);
        if (metadata == nullptr) {
            return TableNotFoundResult{};
        }
        std::optional<Tuple> row =
            metadata->schema.makeTuple(command.values);
        if (!row.has_value()) {
            return SchemaMismatchResult{};
        }
        TableHeap heap(*metadata, *buffer_manager_);
        heap.insert(std::move(*row));
        catalog_->persistTableMetadata();
        reloadDurableCaches();
        return InsertOkResult{};
    }

    Result durableDelete(const DeleteRowsCommand& command) {
        TableMetadata* metadata = findDurableTable(command.table);
        if (metadata == nullptr) {
            return TableNotFoundResult{};
        }
        std::optional<FieldType> column_type =
            metadata->schema.columnType(command.column);
        if (!column_type.has_value()) {
            return SchemaMismatchResult{};
        }
        try {
            Field expected = parseLiteralField(*column_type, command.value);
            TableHeap heap(*metadata, *buffer_manager_);
            size_t deleted = heap.deleteRows(command.column, expected);
            catalog_->persistTableMetadata();
            reloadDurableCaches();
            return DeleteRowsResult{deleted};
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result durableUpdate(const UpdateRowsCommand& command) {
        TableMetadata* metadata = findDurableTable(command.table);
        if (metadata == nullptr) {
            return TableNotFoundResult{};
        }
        std::optional<FieldType> set_type =
            metadata->schema.columnType(command.set_column);
        std::optional<FieldType> where_type =
            metadata->schema.columnType(command.where_column);
        if (!set_type.has_value() || !where_type.has_value()) {
            return SchemaMismatchResult{};
        }
        try {
            Field new_value = parseLiteralField(*set_type, command.set_value);
            Field expected =
                parseLiteralField(*where_type, command.where_value);
            TableHeap heap(*metadata, *buffer_manager_);
            size_t updated = heap.updateRows(
                command.set_column,
                new_value,
                command.where_column,
                expected);
            catalog_->persistTableMetadata();
            reloadDurableCaches();
            return UpdateRowsResult{updated};
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result durableSelectAll(const SelectAllCommand& command) {
        TableMetadata* metadata = findDurableTable(command.table);
        if (metadata == nullptr) {
            return TableNotFoundResult{};
        }
        TableHeap heap(*metadata, *buffer_manager_);
        return SelectAllResult{
            metadata->schema.columnNames(),
            tupleRowsToStrings(heap.readAllTuples())
        };
    }

    Result durableSelectWhere(const SelectWhereCommand& command) {
        TableMetadata* metadata = findDurableTable(command.table);
        if (metadata == nullptr) {
            return TableNotFoundResult{};
        }
        std::optional<FieldType> column_type =
            metadata->schema.columnType(command.column);
        if (!column_type.has_value()) {
            return SchemaMismatchResult{};
        }
        try {
            std::string canonical_value =
                parseLiteralField(*column_type, command.value).toString();
            TableHeap heap(*metadata, *buffer_manager_);
            return SelectAllResult{
                metadata->schema.columnNames(),
                tupleRowsToStrings(heap.readByRecordIDs(
                    index_.lookupRecords(command.table,
                                         command.column,
                                         canonical_value)))
            };
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result durableCount(const CountRowsCommand& command) {
        TableMetadata* metadata = findDurableTable(command.table);
        if (metadata == nullptr) {
            return TableNotFoundResult{};
        }
        return CountRowsResult{metadata->row_count};
    }

    TableMetadata* findDurableTable(const std::string& table) {
        if (!catalog_->hasTable(table)) {
            return nullptr;
        }
        return &catalog_->getTable(table);
    }

    void reloadDurableCaches() {
        tables_ = loadTablesFromCatalog(*catalog_, *buffer_manager_);
        index_.rebuild(*catalog_, *buffer_manager_);
    }

    TableMap tables_;
    HashIndex index_;
    std::optional<std::string> database_filename_;
    std::unique_ptr<BufferManager> buffer_manager_;
    std::unique_ptr<Catalog> catalog_;
};

class BuzzDBApplication : public Application {
public:
    Result execute(const Command& command) override {
        return db_.execute(command);
    }

    std::unique_ptr<Application> clone() const override {
        return std::make_unique<BuzzDBApplication>(*this);
    }

    std::string digest() const override {
        return db_.digest();
    }

private:
    BuzzDBCore db_;
};

class BuzzDBOracle {
public:
    Result execute(const Command& command) {
        return db_.execute(command);
    }

    std::vector<Result> executeAll(const std::vector<Command>& commands) {
        return db_.executeAll(commands);
    }

private:
    BuzzDBCore db_;
};

std::vector<Result> buzzDBOracleResults(const std::vector<Command>& commands) {
    BuzzDBOracle oracle;
    return oracle.executeAll(commands);
}

struct ClientRequest {
    int client_id;
    int request_id;
    Command command;
};

struct ClientReply {
    int client_id;
    int request_id;
    Result result;
};

using Message = std::variant<ClientRequest, ClientReply>;

struct RetryTimer {
    int request_id;
};

using Timer = std::variant<RetryTimer>;

std::string requestKey(int client_id, int request_id) {
    return std::to_string(client_id) + ":" + std::to_string(request_id);
}

std::string describeMessage(const Message& message) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ClientRequest>) {
                out << "ClientRequest(client=" << value.client_id
                    << ", req=" << value.request_id
                    << ", " << describeCommand(value.command) << ")";
            } else if constexpr (std::is_same_v<T, ClientReply>) {
                out << "ClientReply(client=" << value.client_id
                    << ", req=" << value.request_id
                    << ", " << describeResult(value.result) << ")";
            }
            return out.str();
        },
        message);
}

std::string describeTimer(const Timer& timer) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, RetryTimer>) {
                out << "RetryTimer(req=" << value.request_id << ")";
            }
            return out.str();
        },
        timer);
}

struct MessageEnvelope {
    uint64_t id;
    Address from;
    Address to;
    Message message;
};

struct TimerEnvelope {
    uint64_t id;
    Address to;
    Timer timer;
    uint64_t min_delay_ms;
    uint64_t max_delay_ms;
    uint64_t set_order;
};

struct EventRef {
    enum class Kind { Message, Timer };

    Kind kind;
    uint64_t id;

    std::string key() const {
        return std::string(kind == Kind::Message ? "m#" : "tm#") +
               std::to_string(id);
    }
};

struct CheckResult {
    bool ok = true;
    std::string message;
};

constexpr int kDefaultMaxDepth = 20;
constexpr int kDefaultMaxStates = 10000;
constexpr uint32_t kDefaultSearchSeed = 108;
constexpr int kDefaultRandomDfsProbes = 100;

namespace DemoAddress {

Address server1() {
    return Address("server1");
}

Address client1() {
    return Address("client1");
}

Address client2() {
    return Address("client2");
}

}  // namespace DemoAddress

class SearchState;

using Predicate = std::function<CheckResult(const SearchState&)>;

struct NamedPredicate {
    std::string name;
    Predicate predicate;
};

struct SearchSettings {
    int max_depth = kDefaultMaxDepth;
    int max_states = kDefaultMaxStates;
    bool deliver_timers = true;
    bool network_active = true;
    uint32_t seed = kDefaultSearchSeed;
    int random_dfs_probes = kDefaultRandomDfsProbes;
    bool internal_framework_test = false;
    std::string test_title;
    std::vector<std::string> workload_names;
    std::vector<std::string> fault_names;
    std::map<std::pair<Address, Address>, bool> link_active;
    std::map<Address, bool> sender_active;
    std::map<Address, bool> receiver_active;
    std::map<Address, bool> timers_active;
    std::vector<NamedPredicate> invariants;
    std::vector<NamedPredicate> goals;
    std::vector<NamedPredicate> prunes;

    bool shouldDeliver(const MessageEnvelope& envelope) const {
        Address from = envelope.from.rootAddress();
        Address to = envelope.to.rootAddress();
        if (from == to) {
            return true;
        }

        auto link = link_active.find({from, to});
        if (link != link_active.end()) {
            return link->second;
        }

        auto sender = sender_active.find(from);
        if (sender != sender_active.end()) {
            return sender->second;
        }

        auto receiver = receiver_active.find(to);
        if (receiver != receiver_active.end()) {
            return receiver->second;
        }

        return network_active;
    }

    bool deliverTimers(const Address& address) const {
        auto timer = timers_active.find(address.rootAddress());
        if (timer != timers_active.end()) {
            return timer->second;
        }
        return deliver_timers;
    }

    SearchSettings& networkActive(bool active) {
        network_active = active;
        return *this;
    }

    SearchSettings& linkActive(Address from, Address to, bool active) {
        link_active[{from.rootAddress(), to.rootAddress()}] = active;
        return *this;
    }

    SearchSettings& senderActive(Address from, bool active) {
        sender_active[from.rootAddress()] = active;
        return *this;
    }

    SearchSettings& receiverActive(Address to, bool active) {
        receiver_active[to.rootAddress()] = active;
        return *this;
    }

    SearchSettings& nodeActive(Address node, bool active) {
        senderActive(node, active);
        receiverActive(node, active);
        return *this;
    }

    SearchSettings& deliverTimers(bool active) {
        deliver_timers = active;
        return *this;
    }

    SearchSettings& deliverTimers(Address address, bool active) {
        timers_active[address.rootAddress()] = active;
        return *this;
    }

    SearchSettings& timerActive(Address address, bool active) {
        return deliverTimers(std::move(address), active);
    }

    SearchSettings& clearDeliverTimers() {
        deliver_timers = true;
        timers_active.clear();
        return *this;
    }

    SearchSettings& partition(const std::vector<std::vector<Address>>& partitions) {
        network_active = false;
        link_active.clear();
        for (const auto& group : partitions) {
            for (const auto& from : group) {
                for (const auto& to : group) {
                    if (!(from.rootAddress() == to.rootAddress())) {
                        linkActive(from, to, true);
                    }
                }
            }
        }
        return *this;
    }

    SearchSettings& reconnect() {
        network_active = true;
        link_active.clear();
        sender_active.clear();
        receiver_active.clear();
        return *this;
    }

    SearchSettings& resetNetwork() {
        return reconnect();
    }
};

class NodeContext {
public:
    NodeContext(SearchState& state, Address self)
        : state_(state), self_(std::move(self)) {}

    void send(const Address& to, Message message);
    void setTimer(Timer timer, uint64_t delay_ms);
    void setTimer(Timer timer, uint64_t min_delay_ms, uint64_t max_delay_ms);

private:
    SearchState& state_;
    Address self_;
};

class Node {
public:
    explicit Node(Address address) : address_(std::move(address)) {}
    virtual ~Node() = default;

    const Address& address() const {
        return address_;
    }

    virtual void init(NodeContext& ctx) {
        (void) ctx;
    }

    virtual void onMessage(NodeContext& ctx,
                           const Address& from,
                           const Message& message) = 0;

    virtual void onTimer(NodeContext& ctx, const Timer& timer) {
        (void) ctx;
        (void) timer;
    }

    virtual std::unique_ptr<Node> clone() const = 0;
    virtual std::string digest() const = 0;
    virtual std::string describe() const = 0;

private:
    Address address_;
};

class Workload {
public:
    Workload(std::vector<Command> commands, std::vector<Result> expected_results)
        : commands_(std::move(commands)),
          expected_results_(std::move(expected_results)) {
        if (!expected_results_.empty() &&
            expected_results_.size() != commands_.size()) {
            throw std::runtime_error("Workload commands/results size mismatch.");
        }
    }

    bool hasNext() const {
        return index_ < commands_.size();
    }

    bool hasExpectedResults() const {
        return !expected_results_.empty();
    }

    Command nextCommand() {
        if (!hasNext()) {
            throw std::runtime_error("Workload is finished.");
        }
        return commands_[index_++];
    }

    Result expectedResult(size_t index) const {
        return expected_results_.at(index);
    }

    size_t expectedCount() const {
        return expected_results_.size();
    }

    std::string digest() const {
        return "Workload(index=" + std::to_string(index_) +
               ", size=" + std::to_string(commands_.size()) + ")";
    }

private:
    std::vector<Command> commands_;
    std::vector<Result> expected_results_;
    size_t index_ = 0;
};

class SearchState {
public:
    SearchState() = default;

    SearchState(const SearchState& other)
        : network_(other.network_),
          timers_(other.timers_),
          new_messages_(other.new_messages_),
          new_timers_(other.new_timers_),
          history_(other.history_),
          depth_(other.depth_),
          next_message_id_(other.next_message_id_),
          next_timer_id_(other.next_timer_id_),
          next_timer_order_(other.next_timer_order_),
          exception_(other.exception_) {
        for (const auto& entry : other.nodes_) {
            nodes_.emplace(entry.first, entry.second->clone());
        }
    }

    SearchState& operator=(const SearchState& other) {
        if (this == &other) {
            return *this;
        }
        nodes_.clear();
        for (const auto& entry : other.nodes_) {
            nodes_.emplace(entry.first, entry.second->clone());
        }
        network_ = other.network_;
        timers_ = other.timers_;
        new_messages_ = other.new_messages_;
        new_timers_ = other.new_timers_;
        history_ = other.history_;
        depth_ = other.depth_;
        next_message_id_ = other.next_message_id_;
        next_timer_id_ = other.next_timer_id_;
        next_timer_order_ = other.next_timer_order_;
        exception_ = other.exception_;
        return *this;
    }

    void addNode(std::unique_ptr<Node> node) {
        Address address = node->address().rootAddress();
        nodes_[address] = std::move(node);
        timers_[address];
        NodeContext ctx(*this, address);
        nodes_[address]->init(ctx);
    }

    void send(const Address& from, const Address& to, Message message) {
        MessageEnvelope envelope{
            next_message_id_++,
            from,
            to,
            std::move(message)
        };
        network_.push_back(envelope);
        new_messages_.push_back(std::move(envelope));
    }

    void setTimer(const Address& to,
                  Timer timer,
                  uint64_t min_delay_ms,
                  uint64_t max_delay_ms) {
        TimerEnvelope envelope{
            next_timer_id_++,
            to,
            std::move(timer),
            min_delay_ms,
            max_delay_ms,
            next_timer_order_++
        };
        timers_[to.rootAddress()].push_back(envelope);
        new_timers_.push_back(std::move(envelope));
    }

    std::vector<EventRef> events() const {
        SearchSettings settings;
        return events(settings);
    }

    std::vector<EventRef> events(const SearchSettings& settings) const {
        std::vector<EventRef> out;
        for (const auto& message : network_) {
            if (nodes_.find(message.to.rootAddress()) != nodes_.end() &&
                settings.shouldDeliver(message)) {
                out.push_back({EventRef::Kind::Message, message.id});
            }
        }

        for (const auto& entry : timers_) {
            if (!settings.deliverTimers(entry.first)) {
                continue;
            }
            uint64_t min_seen_max = 0;
            bool have_min = false;
            for (const auto& timer : entry.second) {
                if (have_min && timer.min_delay_ms >= min_seen_max) {
                    continue;
                }
                out.push_back({EventRef::Kind::Timer, timer.id});
                if (!have_min || timer.max_delay_ms < min_seen_max) {
                    min_seen_max = timer.max_delay_ms;
                    have_min = true;
                }
            }
        }
        return out;
    }

    std::optional<SearchState> stepEvent(const EventRef& event,
                                         const SearchSettings& settings) const {
        auto available = events(settings);
        auto match = std::find_if(
            available.begin(),
            available.end(),
            [&](const EventRef& candidate) {
                return candidate.kind == event.kind && candidate.id == event.id;
            });
        if (match == available.end()) {
            return std::nullopt;
        }
        return stepEvent(event);
    }

    std::vector<EventRef> messageEventsMatching(
        const SearchSettings& settings,
        const std::function<bool(const MessageEnvelope&)>& predicate) const {
        std::vector<EventRef> out;
        for (const auto& message : network_) {
            if (nodes_.find(message.to.rootAddress()) != nodes_.end() &&
                settings.shouldDeliver(message) &&
                predicate(message)) {
                out.push_back({EventRef::Kind::Message, message.id});
            }
        }
        return out;
    }

    std::optional<SearchState> stepMessageMatching(
        const SearchSettings& settings,
        const std::function<bool(const MessageEnvelope&)>& predicate) const {
        std::vector<EventRef> matches = messageEventsMatching(settings, predicate);
        if (matches.empty()) {
            return std::nullopt;
        }
        return stepEvent(matches.front(), settings);
    }

    std::optional<SearchState> stepEvent(const EventRef& event) const {
        SearchState next(*this);
        next.depth_++;
        next.history_.push_back(event.key());
        next.new_messages_.clear();
        next.new_timers_.clear();

        try {
            if (event.kind == EventRef::Kind::Message) {
                auto message = std::find_if(
                    next.network_.begin(),
                    next.network_.end(),
                    [&](const MessageEnvelope& envelope) {
                        return envelope.id == event.id;
                    });
                if (message == next.network_.end()) {
                    return std::nullopt;
                }

                auto node = next.nodes_.find(message->to.rootAddress());
                if (node == next.nodes_.end()) {
                    return std::nullopt;
                }

                NodeContext ctx(next, message->to.rootAddress());
                node->second->onMessage(ctx, message->from, message->message);
                return next;
            }

            for (auto& entry : next.timers_) {
                auto timer = std::find_if(
                    entry.second.begin(),
                    entry.second.end(),
                    [&](const TimerEnvelope& envelope) {
                        return envelope.id == event.id;
                    });
                if (timer == entry.second.end()) {
                    continue;
                }

                TimerEnvelope timer_copy = *timer;
                entry.second.erase(timer);
                auto node = next.nodes_.find(timer_copy.to.rootAddress());
                if (node == next.nodes_.end()) {
                    return std::nullopt;
                }

                NodeContext ctx(next, timer_copy.to.rootAddress());
                node->second->onTimer(ctx, timer_copy.timer);
                return next;
            }
        } catch (const std::exception& e) {
            next.exception_ = e.what();
            return next;
        }

        return std::nullopt;
    }

    template <typename T>
    const T* nodeAs(const Address& address) const {
        auto it = nodes_.find(address.rootAddress());
        if (it == nodes_.end()) {
            return nullptr;
        }
        return dynamic_cast<const T*>(it->second.get());
    }

    bool hasNode(const Address& address) const {
        return nodes_.find(address.rootAddress()) != nodes_.end();
    }

    const std::vector<MessageEnvelope>& network() const {
        return network_;
    }

    const std::vector<MessageEnvelope>& newMessages() const {
        return new_messages_;
    }

    const std::map<Address, std::vector<TimerEnvelope>>& timers() const {
        return timers_;
    }

    const std::vector<TimerEnvelope>& newTimers() const {
        return new_timers_;
    }

    int depth() const {
        return depth_;
    }

    const std::vector<std::string>& history() const {
        return history_;
    }

    const std::string& exception() const {
        return exception_;
    }

    std::string digest() const {
        std::ostringstream out;
        out << "depth=" << depth_;
        for (const auto& entry : nodes_) {
            out << ";node(" << entry.first.str() << ")=" << entry.second->digest();
        }
        out << ";network=";
        for (const auto& message : network_) {
            out << "{" << message.id << ":" << message.from.str()
                << ">" << message.to.str() << ":" << describeMessage(message.message)
                << "}";
        }
        out << ";timers=";
        for (const auto& entry : timers_) {
            for (const auto& timer : entry.second) {
                out << "{" << timer.id << ":" << timer.to.str()
                    << ":" << describeTimer(timer.timer) << "}";
            }
        }
        return out.str();
    }

    void printSummary() const {
        std::cout << "State depth=" << depth_ << std::endl;
        for (const auto& entry : nodes_) {
            std::cout << "  " << entry.first << " -> "
                      << entry.second->describe() << std::endl;
        }
        std::cout << "  network:";
        for (const auto& message : network_) {
            std::cout << " m#" << message.id;
        }
        std::cout << std::endl;
        std::cout << "  timers:";
        for (const auto& entry : timers_) {
            for (const auto& timer : entry.second) {
                std::cout << " tm#" << timer.id;
            }
        }
        std::cout << std::endl;
    }

private:
    std::map<Address, std::unique_ptr<Node>> nodes_;
    std::vector<MessageEnvelope> network_;
    std::map<Address, std::vector<TimerEnvelope>> timers_;
    std::vector<MessageEnvelope> new_messages_;
    std::vector<TimerEnvelope> new_timers_;
    std::vector<std::string> history_;
    int depth_ = 0;
    uint64_t next_message_id_ = 1;
    uint64_t next_timer_id_ = 1;
    uint64_t next_timer_order_ = 1;
    std::string exception_;
};

void NodeContext::send(const Address& to, Message message) {
    state_.send(self_, to, std::move(message));
}

void NodeContext::setTimer(Timer timer, uint64_t delay_ms) {
    setTimer(std::move(timer), delay_ms, delay_ms);
}

void NodeContext::setTimer(Timer timer,
                           uint64_t min_delay_ms,
                           uint64_t max_delay_ms) {
    state_.setTimer(self_, std::move(timer), min_delay_ms, max_delay_ms);
}

class AtMostOnceServer : public Node {
public:
    AtMostOnceServer(Address address, std::unique_ptr<Application> app)
        : Node(std::move(address)), app_(std::move(app)) {}

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        const auto* request = std::get_if<ClientRequest>(&message);
        if (request == nullptr) {
            return;
        }

        std::string key = requestKey(request->client_id, request->request_id);
        auto cached = cache_.find(key);
        if (cached == cache_.end()) {
            execution_count_[key]++;
            Result result = app_->execute(request->command);
            cached = cache_.emplace(key, result).first;
        }

        ctx.send(from, ClientReply{
            request->client_id,
            request->request_id,
            cached->second
        });
    }

    std::unique_ptr<Node> clone() const override {
        return std::unique_ptr<Node>(
            new AtMostOnceServer(address(), app_->clone(), cache_, execution_count_));
    }

    int maxExecutionCount() const {
        int max_seen = 0;
        for (const auto& entry : execution_count_) {
            max_seen = std::max(max_seen, entry.second);
        }
        return max_seen;
    }

    std::string digest() const override {
        std::ostringstream out;
        out << "Server(cache=" << cache_.size()
            << ", max_exec=" << maxExecutionCount()
            << ", app=" << app_->digest() << ")";
        return out.str();
    }

    std::string describe() const override {
        return digest();
    }

private:
    AtMostOnceServer(Address address,
                    std::unique_ptr<Application> app,
                    std::map<std::string, Result> cache,
                    std::map<std::string, int> execution_count)
        : Node(std::move(address)),
          app_(std::move(app)),
          cache_(std::move(cache)),
          execution_count_(std::move(execution_count)) {}

    std::unique_ptr<Application> app_;
    std::map<std::string, Result> cache_;
    std::map<std::string, int> execution_count_;
};

class ClientWorker : public Node {
public:
    ClientWorker(Address address,
                 Address server,
                 int client_id,
                 Workload workload)
        : Node(std::move(address)),
          server_(std::move(server)),
          client_id_(client_id),
          workload_(std::move(workload)) {}

    void init(NodeContext& ctx) override {
        sendNext(ctx);
    }

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        (void) ctx;
        (void) from;
        const auto* reply = std::get_if<ClientReply>(&message);
        if (reply == nullptr) {
            return;
        }
        if (!waiting_ ||
            reply->client_id != client_id_ ||
            reply->request_id != in_flight_request_id_) {
            ++stale_replies_;
            return;
        }

        results_.push_back(reply->result);
        waiting_ = false;
        in_flight_request_id_ = 0;
        sendNext(ctx);
    }

    void onTimer(NodeContext& ctx, const Timer& timer) override {
        const auto* retry = std::get_if<RetryTimer>(&timer);
        if (retry != nullptr &&
            waiting_ &&
            retry->request_id == in_flight_request_id_) {
            ++retries_;
            sendInFlight(ctx);
        }
    }

    std::unique_ptr<Node> clone() const override {
        return std::make_unique<ClientWorker>(*this);
    }

    bool done() const {
        return !waiting_ && !workload_.hasNext();
    }

    size_t resultCount() const {
        return results_.size();
    }

    const std::vector<Command>& sentCommands() const {
        return sent_commands_;
    }

    const std::vector<Result>& results() const {
        return results_;
    }

    bool resultsOk() const {
        if (!workload_.hasExpectedResults()) {
            return true;
        }
        if (results_.size() > workload_.expectedCount()) {
            return false;
        }
        for (size_t i = 0; i < results_.size(); ++i) {
            if (!(results_[i] == workload_.expectedResult(i))) {
                return false;
            }
        }
        return true;
    }

    std::string digest() const override {
        std::ostringstream out;
        out << "Client(waiting=" << waiting_
            << ", req=" << in_flight_request_id_
            << ", results=" << results_.size()
            << ", stale=" << stale_replies_
            << ", retries=" << retries_
            << ", workload=" << workload_.digest() << ")";
        return out.str();
    }

    std::string describe() const override {
        std::ostringstream out;
        out << "ClientWorker{done=" << (done() ? "true" : "false")
            << ", results_ok=" << (resultsOk() ? "true" : "false")
            << ", results=[";
        for (size_t i = 0; i < results_.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << describeResult(results_[i]);
        }
        out << "], retries=" << retries_
            << ", stale=" << stale_replies_ << "}";
        return out.str();
    }

private:
    void sendNext(NodeContext& ctx) {
        if (!workload_.hasNext()) {
            return;
        }
        waiting_ = true;
        in_flight_request_id_ = next_request_id_++;
        in_flight_command_ = workload_.nextCommand();
        sent_commands_.push_back(in_flight_command_);
        sendInFlight(ctx);
    }

    void sendInFlight(NodeContext& ctx) {
        ctx.send(server_, ClientRequest{
            client_id_,
            in_flight_request_id_,
            in_flight_command_
        });
        ctx.setTimer(RetryTimer{in_flight_request_id_}, 5, 7);
    }

    Address server_;
    int client_id_;
    Workload workload_;
    bool waiting_ = false;
    int next_request_id_ = 1;
    int in_flight_request_id_ = 0;
    Command in_flight_command_ = CreateTableCommand{"", {""}};
    int retries_ = 0;
    int stale_replies_ = 0;
    std::vector<Command> sent_commands_;
    std::vector<Result> results_;
};

struct SearchResults {
    enum class EndCondition {
        SpaceExhausted,
        StateLimit,
        InvariantViolated,
        GoalFound,
        ExceptionThrown
    };

    EndCondition end_condition = EndCondition::SpaceExhausted;
    std::string message;
    std::optional<SearchState> terminal_state;
    int explored = 0;
    std::string test_title;
    std::vector<std::string> workload_names;
    std::vector<std::string> fault_names;
    bool internal_framework_test = false;
};

void attachSearchMetadata(SearchResults& results, const SearchSettings& settings) {
    results.test_title = settings.test_title;
    results.workload_names = settings.workload_names;
    results.fault_names = settings.fault_names;
    results.internal_framework_test = settings.internal_framework_test;
}

std::string endConditionName(SearchResults::EndCondition condition) {
    switch (condition) {
        case SearchResults::EndCondition::SpaceExhausted:
            return "space exhausted";
        case SearchResults::EndCondition::StateLimit:
            return "state limit";
        case SearchResults::EndCondition::InvariantViolated:
            return "invariant violated";
        case SearchResults::EndCondition::GoalFound:
            return "goal found";
        case SearchResults::EndCondition::ExceptionThrown:
            return "exception thrown";
    }
    return "unknown";
}

std::optional<SearchResults> checkState(const SearchState& state,
                                        const SearchSettings& settings) {
    if (!state.exception().empty()) {
        SearchResults results;
        results.end_condition = SearchResults::EndCondition::ExceptionThrown;
        results.message = state.exception();
        results.terminal_state = state;
        attachSearchMetadata(results, settings);
        return results;
    }

    for (const auto& invariant : settings.invariants) {
        CheckResult result = invariant.predicate(state);
        if (!result.ok) {
            SearchResults results;
            results.end_condition =
                SearchResults::EndCondition::InvariantViolated;
            results.message = invariant.name + ": " + result.message;
            results.terminal_state = state;
            attachSearchMetadata(results, settings);
            return results;
        }
    }

    for (const auto& goal : settings.goals) {
        CheckResult result = goal.predicate(state);
        if (result.ok) {
            SearchResults results;
            results.end_condition = SearchResults::EndCondition::GoalFound;
            results.message = goal.name;
            results.terminal_state = state;
            attachSearchMetadata(results, settings);
            return results;
        }
    }

    return std::nullopt;
}

bool shouldPrune(const SearchState& state, const SearchSettings& settings) {
    if (state.depth() >= settings.max_depth) {
        return true;
    }
    for (const auto& prune : settings.prunes) {
        if (prune.predicate(state).ok) {
            return true;
        }
    }
    return false;
}

SearchResults bfs(const SearchState& initial, const SearchSettings& settings) {
    std::deque<SearchState> queue;
    std::set<std::string> seen;
    queue.push_back(initial);
    seen.insert(initial.digest());

    SearchResults results;
    attachSearchMetadata(results, settings);
    while (!queue.empty()) {
        SearchState current = queue.front();
        queue.pop_front();
        results.explored++;

        if (auto terminal = checkState(current, settings)) {
            terminal->explored = results.explored;
            return *terminal;
        }

        if (shouldPrune(current, settings)) {
            continue;
        }

        if (results.explored >= settings.max_states) {
            results.end_condition = SearchResults::EndCondition::StateLimit;
            results.message = "state limit reached";
            results.terminal_state = current;
            return results;
        }

        for (const auto& event : current.events(settings)) {
            auto next = current.stepEvent(event, settings);
            if (!next) {
                continue;
            }
            std::string key = next->digest();
            if (seen.insert(key).second) {
                queue.push_back(*next);
            }
        }
    }

    results.end_condition = SearchResults::EndCondition::SpaceExhausted;
    results.message = "all reachable states searched";
    return results;
}

SearchResults randomDfs(const SearchState& initial,
                        SearchSettings settings) {
    std::mt19937 rng(settings.seed);
    SearchResults aggregate;
    attachSearchMetadata(aggregate, settings);
    for (int probe = 0; probe < settings.random_dfs_probes; ++probe) {
        SearchState current = initial;
        for (;;) {
            aggregate.explored++;
            if (auto terminal = checkState(current, settings)) {
                terminal->explored = aggregate.explored;
                return *terminal;
            }
            if (shouldPrune(current, settings)) {
                break;
            }
            auto events = current.events(settings);
            if (events.empty()) {
                break;
            }
            std::shuffle(events.begin(), events.end(), rng);
            auto next = current.stepEvent(events.front(), settings);
            if (!next) {
                break;
            }
            current = *next;
            if (aggregate.explored >= settings.max_states) {
                aggregate.end_condition = SearchResults::EndCondition::StateLimit;
                aggregate.message = "state limit reached";
                aggregate.terminal_state = current;
                return aggregate;
            }
        }
    }
    aggregate.end_condition = SearchResults::EndCondition::SpaceExhausted;
    aggregate.message = "random DFS probes completed";
    return aggregate;
}

SearchResults replayTrace(SearchState state,
                          const SearchSettings& settings,
                          const std::vector<std::string>& event_keys) {
    SearchResults results;
    attachSearchMetadata(results, settings);
    for (const auto& key : event_keys) {
        auto events = state.events(settings);
        auto event = std::find_if(
            events.begin(),
            events.end(),
            [&](const EventRef& candidate) {
                return candidate.key() == key;
            });
        if (event == events.end()) {
            results.end_condition = SearchResults::EndCondition::ExceptionThrown;
            results.message = "could not replay event " + key;
            results.terminal_state = state;
            return results;
        }
        auto next = state.stepEvent(*event, settings);
        if (!next) {
            results.end_condition = SearchResults::EndCondition::ExceptionThrown;
            results.message = "event became undeliverable " + key;
            results.terminal_state = state;
            return results;
        }
        state = *next;
        results.explored++;
        if (auto terminal = checkState(state, settings)) {
            terminal->explored = results.explored;
            return *terminal;
        }
    }
    results.end_condition = SearchResults::EndCondition::SpaceExhausted;
    results.message = "trace replay completed";
    results.terminal_state = state;
    return results;
}

CheckResult resultsOk(const SearchState& state, const Address& client) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    if (!worker->resultsOk()) {
        return {false, client.str() + " received an unexpected result"};
    }
    return {true, ""};
}

CheckResult resultsOk(const SearchState& state,
                      const std::vector<Address>& clients) {
    for (const auto& client : clients) {
        CheckResult result = resultsOk(state, client);
        if (!result.ok) {
            return result;
        }
    }
    return {true, ""};
}

CheckResult clientDone(const SearchState& state, const Address& client) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    return {worker->done(), client.str() + " is not done"};
}

CheckResult clientsDone(const SearchState& state,
                        const std::vector<Address>& clients) {
    for (const auto& client : clients) {
        CheckResult result = clientDone(state, client);
        if (!result.ok) {
            return result;
        }
    }
    return {true, ""};
}

CheckResult clientHasExactlyResults(const SearchState& state,
                                    const Address& client,
                                    size_t count) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    return {worker->resultCount() == count,
            client.str() + " has " + std::to_string(worker->resultCount()) +
                " results, expected exactly " + std::to_string(count)};
}

CheckResult clientHasAtLeastResults(const SearchState& state,
                                    const Address& client,
                                    size_t count) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    return {worker->resultCount() >= count,
            client.str() + " has fewer results than requested"};
}

CheckResult clientHasResults(const SearchState& state,
                             const Address& client,
                             size_t count) {
    return clientHasExactlyResults(state, client, count);
}

CheckResult clientHasResult(const SearchState& state,
                            const Address& client,
                            size_t count = 1) {
    return clientHasAtLeastResults(state, client, count);
}

CheckResult serverAtMostOnce(const SearchState& state, const Address& server) {
    const auto* node = state.nodeAs<AtMostOnceServer>(server);
    if (node == nullptr) {
        return {false, server.str() + " is missing"};
    }
    if (node->maxExecutionCount() > 1) {
        return {false, "server executed a request more than once"};
    }
    return {true, ""};
}

CheckResult noInvariantViolation(const SearchState& state,
                                 const std::vector<NamedPredicate>& invariants) {
    for (const auto& invariant : invariants) {
        CheckResult result = invariant.predicate(state);
        if (!result.ok) {
            return {false, invariant.name + ": " + result.message};
        }
    }
    return {true, ""};
}

CheckResult noneDecided(const SearchState& state,
                        const std::vector<Address>& clients) {
    for (const auto& client : clients) {
        const auto* worker = state.nodeAs<ClientWorker>(client);
        if (worker == nullptr) {
            return {false, client.str() + " is missing"};
        }
        if (worker->resultCount() != 0) {
            return {false, client.str() + " received a result"};
        }
    }
    return {true, ""};
}

CheckResult allResultsSame(const SearchState& state,
                           const std::vector<Address>& clients) {
    std::optional<std::vector<Result>> first;
    for (const auto& client : clients) {
        const auto* worker = state.nodeAs<ClientWorker>(client);
        if (worker == nullptr) {
            return {false, client.str() + " is missing"};
        }
        if (!first) {
            first = worker->results();
        } else if (!(worker->results() == *first)) {
            return {false, client.str() + " results differ"};
        }
    }
    return {true, ""};
}

CheckResult containsEnvelopeMatching(
    const SearchState& state,
    const std::function<bool(const MessageEnvelope&)>& predicate,
    const std::string& description = "message envelope") {
    for (const auto& envelope : state.network()) {
        if (predicate(envelope)) {
            return {true, ""};
        }
    }
    return {false, "network does not contain " + description};
}

CheckResult containsMessageMatching(
    const SearchState& state,
    const std::function<bool(const Message&)>& predicate,
    const std::string& description = "message") {
    return containsEnvelopeMatching(
        state,
        [&](const MessageEnvelope& envelope) {
            return predicate(envelope.message);
        },
        description);
}

CheckResult messageInFlight(const SearchState& state,
                            std::optional<Address> from = std::nullopt,
                            std::optional<Address> to = std::nullopt) {
    for (const auto& message : state.network()) {
        if (from && !(message.from.rootAddress() == from->rootAddress())) {
            continue;
        }
        if (to && !(message.to.rootAddress() == to->rootAddress())) {
            continue;
        }
        return {true, ""};
    }

    std::ostringstream out;
    out << "no message in flight";
    if (from) {
        out << " from " << from->str();
    }
    if (to) {
        out << " to " << to->str();
    }
    return {false, out.str()};
}

CheckResult timerPending(const SearchState& state,
                         std::optional<Address> to = std::nullopt) {
    for (const auto& entry : state.timers()) {
        if (to && !(entry.first.rootAddress() == to->rootAddress())) {
            continue;
        }
        if (!entry.second.empty()) {
            return {true, ""};
        }
    }

    std::ostringstream out;
    out << "no timer pending";
    if (to) {
        out << " for " << to->str();
    }
    return {false, out.str()};
}

namespace Predicates {

NamedPredicate resultsOk(std::vector<Address> clients) {
    return {
        "RESULTS_OK",
        [clients = std::move(clients)](const SearchState& state) {
            return ::resultsOk(state, clients);
        }
    };
}

NamedPredicate clientsDone(std::vector<Address> clients) {
    return {
        "CLIENTS_DONE",
        [clients = std::move(clients)](const SearchState& state) {
            return ::clientsDone(state, clients);
        }
    };
}

NamedPredicate clientHasResult(Address client, size_t count = 1) {
    return {
        "CLIENT_HAS_RESULT",
        [client = std::move(client), count](const SearchState& state) {
            return ::clientHasResult(state, client, count);
        }
    };
}

NamedPredicate clientHasExactlyResults(Address client, size_t count) {
    return {
        "CLIENT_HAS_EXACTLY_RESULTS",
        [client = std::move(client), count](const SearchState& state) {
            return ::clientHasExactlyResults(state, client, count);
        }
    };
}

NamedPredicate noneDecided(std::vector<Address> clients) {
    return {
        "NONE_DECIDED",
        [clients = std::move(clients)](const SearchState& state) {
            return ::noneDecided(state, clients);
        }
    };
}

NamedPredicate allResultsSame(std::vector<Address> clients) {
    return {
        "ALL_RESULTS_SAME",
        [clients = std::move(clients)](const SearchState& state) {
            return ::allResultsSame(state, clients);
        }
    };
}

NamedPredicate containsEnvelopeMatching(
    std::string name,
    std::function<bool(const MessageEnvelope&)> predicate) {
    return {
        "CONTAINS_ENVELOPE_MATCHING",
        [name = std::move(name),
         predicate = std::move(predicate)](const SearchState& state) {
            return ::containsEnvelopeMatching(state, predicate, name);
        }
    };
}

NamedPredicate containsMessageMatching(
    std::string name,
    std::function<bool(const Message&)> predicate) {
    return {
        "CONTAINS_MESSAGE_MATCHING",
        [name = std::move(name),
         predicate = std::move(predicate)](const SearchState& state) {
            return ::containsMessageMatching(state, predicate, name);
        }
    };
}

NamedPredicate noInvariantViolation(std::vector<NamedPredicate> invariants) {
    return {
        "NO_INVARIANT_VIOLATION",
        [invariants = std::move(invariants)](const SearchState& state) {
            return ::noInvariantViolation(state, invariants);
        }
    };
}

NamedPredicate messageInFlight(std::optional<Address> from = std::nullopt,
                               std::optional<Address> to = std::nullopt) {
    return {
        "MESSAGE_IN_FLIGHT",
        [from = std::move(from), to = std::move(to)](const SearchState& state) {
            return ::messageInFlight(state, from, to);
        }
    };
}

NamedPredicate timerPending(std::optional<Address> to = std::nullopt) {
    return {
        "TIMER_PENDING",
        [to = std::move(to)](const SearchState& state) {
            return ::timerPending(state, to);
        }
    };
}

NamedPredicate atMostOnce(Address server) {
    return {
        "AT_MOST_ONCE",
        [server = std::move(server)](const SearchState& state) {
            return serverAtMostOnce(state, server);
        }
    };
}

}  // namespace Predicates

struct Scenario {
    SearchState state;
    SearchSettings settings;
};

class FaultWorkload {
public:
    virtual ~FaultWorkload() = default;
    virtual std::string description() const = 0;
    virtual void apply(SearchSettings& settings) const = 0;
};

class PartitionFaultWorkload final : public FaultWorkload {
public:
    explicit PartitionFaultWorkload(std::vector<std::vector<Address>> partitions)
        : partitions_(std::move(partitions)) {}

    std::string description() const override {
        return "Partition";
    }

    void apply(SearchSettings& settings) const override {
        settings.partition(partitions_);
    }

private:
    std::vector<std::vector<Address>> partitions_;
};

class NodeUnavailableFault final : public FaultWorkload {
public:
    explicit NodeUnavailableFault(std::vector<Address> nodes,
                                  bool pause_timers = true)
        : nodes_(std::move(nodes)), pause_timers_(pause_timers) {}

    std::string description() const override {
        return "NodeUnavailable";
    }

    void apply(SearchSettings& settings) const override {
        for (const auto& node : nodes_) {
            settings.nodeActive(node, false);
            if (pause_timers_) {
                settings.timerActive(node, false);
            }
        }
    }

private:
    std::vector<Address> nodes_;
    bool pause_timers_;
};

class TimerPauseFaultWorkload final : public FaultWorkload {
public:
    explicit TimerPauseFaultWorkload(std::vector<Address> nodes)
        : nodes_(std::move(nodes)) {}

    std::string description() const override {
        return "TimerPause";
    }

    void apply(SearchSettings& settings) const override {
        for (const auto& node : nodes_) {
            settings.timerActive(node, false);
        }
    }

private:
    std::vector<Address> nodes_;
};

class RandomDisabledLinksFault final : public FaultWorkload {
public:
    RandomDisabledLinksFault(std::vector<Address> nodes,
                             int disabled_pairs,
                             uint32_t seed)
        : nodes_(std::move(nodes)),
          disabled_pairs_(disabled_pairs),
          seed_(seed) {}

    std::string description() const override {
        return "RandomDisabledLinks";
    }

    void apply(SearchSettings& settings) const override {
        if (nodes_.size() < 2 || disabled_pairs_ <= 0) {
            return;
        }

        std::mt19937 rng(seed_);
        std::uniform_int_distribution<size_t> pick(0, nodes_.size() - 1);
        for (int i = 0; i < disabled_pairs_; ++i) {
            Address from = nodes_[pick(rng)];
            Address to = nodes_[pick(rng)];
            if (from == to) {
                to = nodes_[(pick(rng) + 1) % nodes_.size()];
            }
            settings.linkActive(from, to, false);
        }
    }

private:
    std::vector<Address> nodes_;
    int disabled_pairs_;
    uint32_t seed_;
};

class ScenarioBuilder;

class SearchTestWorkload {
public:
    virtual ~SearchTestWorkload() = default;
    virtual std::string description() const = 0;
    virtual void setup(ScenarioBuilder& builder) const {
        (void) builder;
    }
    virtual void configure(SearchSettings& settings) const = 0;
    virtual CheckResult check(const SearchState& state) const = 0;
};

class ClientCompletionWorkload final : public SearchTestWorkload {
public:
    explicit ClientCompletionWorkload(std::vector<Address> clients)
        : clients_(std::move(clients)) {}

    std::string description() const override {
        return "ClientCompletion";
    }

    void configure(SearchSettings& settings) const override {
        settings.invariants.push_back(Predicates::resultsOk(clients_));
        settings.goals.push_back(Predicates::clientsDone(clients_));
    }

    CheckResult check(const SearchState& state) const override {
        return clientsDone(state, clients_);
    }

private:
    std::vector<Address> clients_;
};

class CompoundSearchWorkload {
public:
    CompoundSearchWorkload& addWorkload(std::shared_ptr<const SearchTestWorkload> workload) {
        if (!workload) {
            throw std::runtime_error("CompoundSearchWorkload cannot add a null workload.");
        }
        workloads_.push_back(std::move(workload));
        return *this;
    }

    CompoundSearchWorkload& addFailure(std::shared_ptr<const FaultWorkload> failure) {
        if (!failure) {
            throw std::runtime_error("CompoundSearchWorkload cannot add a null failure.");
        }
        failures_.push_back(std::move(failure));
        return *this;
    }

    std::string description() const {
        std::ostringstream out;
        bool first = true;
        for (const auto& workload : workloads_) {
            if (!first) {
                out << ";";
            }
            out << workload->description();
            first = false;
        }
        for (const auto& failure : failures_) {
            if (!first) {
                out << ";";
            }
            out << failure->description();
            first = false;
        }
        return out.str();
    }

    void setup(ScenarioBuilder& builder) const {
        for (const auto& workload : workloads_) {
            workload->setup(builder);
        }
    }

    void configure(SearchSettings& settings) const {
        for (const auto& failure : failures_) {
            settings.fault_names.push_back(failure->description());
            failure->apply(settings);
        }
        for (const auto& workload : workloads_) {
            settings.workload_names.push_back(workload->description());
            workload->configure(settings);
        }
    }

    CheckResult check(const SearchState& state) const {
        for (const auto& workload : workloads_) {
            CheckResult result = workload->check(state);
            if (!result.ok) {
                return result;
            }
        }
        return {true, ""};
    }

private:
    std::vector<std::shared_ptr<const SearchTestWorkload>> workloads_;
    std::vector<std::shared_ptr<const FaultWorkload>> failures_;
};

class ScenarioBuilder {
public:
    ScenarioBuilder& addNode(std::unique_ptr<Node> node) {
        if (!node) {
            throw std::runtime_error("ScenarioBuilder cannot add a null node.");
        }
        std::shared_ptr<const Node> prototype(std::move(node));
        node_factories_.push_back([prototype]() {
            return prototype->clone();
        });
        return *this;
    }

    ScenarioBuilder& addServer(Address address, std::unique_ptr<Application> app) {
        return addNode(std::make_unique<AtMostOnceServer>(
            std::move(address),
            std::move(app)));
    }

    ScenarioBuilder& addServer(
        Address address,
        std::function<std::unique_ptr<Application>()> app_factory) {
        node_factories_.push_back(
            [address = std::move(address),
             app_factory = std::move(app_factory)]() {
                return std::make_unique<AtMostOnceServer>(
                    address,
                    app_factory());
            });
        return *this;
    }

    ScenarioBuilder& addClient(Address address,
                               Address server,
                               int client_id,
                               Workload workload) {
        node_factories_.push_back(
            [address = std::move(address),
             server = std::move(server),
             client_id,
             workload = std::move(workload)]() {
                return std::make_unique<ClientWorker>(
                    address,
                    server,
                    client_id,
                    workload);
            });
        return *this;
    }

    ScenarioBuilder& partition(
        const std::vector<std::vector<Address>>& partitions) {
        settings_.partition(partitions);
        return *this;
    }

    ScenarioBuilder& resetNetwork() {
        settings_.resetNetwork();
        return *this;
    }

    ScenarioBuilder& nodeActive(Address address, bool active) {
        settings_.nodeActive(std::move(address), active);
        return *this;
    }

    ScenarioBuilder& linkActive(Address from, Address to, bool active) {
        settings_.linkActive(std::move(from), std::move(to), active);
        return *this;
    }

    ScenarioBuilder& senderActive(Address from, bool active) {
        settings_.senderActive(std::move(from), active);
        return *this;
    }

    ScenarioBuilder& receiverActive(Address to, bool active) {
        settings_.receiverActive(std::move(to), active);
        return *this;
    }

    ScenarioBuilder& timerActive(Address address, bool active) {
        settings_.timerActive(std::move(address), active);
        return *this;
    }

    ScenarioBuilder& addFault(std::shared_ptr<const FaultWorkload> fault) {
        if (!fault) {
            throw std::runtime_error("ScenarioBuilder cannot add a null fault.");
        }
        faults_.push_back(std::move(fault));
        return *this;
    }

    SearchSettings& settings() {
        return settings_;
    }

    const SearchSettings& settings() const {
        return settings_;
    }

    SearchState buildSearchState() const {
        SearchState state;
        for (const auto& factory : node_factories_) {
            state.addNode(factory());
        }
        return state;
    }

    SearchSettings buildSearchSettings() const {
        SearchSettings settings = settings_;
        for (const auto& fault : faults_) {
            settings.fault_names.push_back(fault->description());
            fault->apply(settings);
        }
        return settings;
    }

    Scenario build() const {
        Scenario scenario;
        scenario.state = buildSearchState();
        scenario.settings = buildSearchSettings();
        return scenario;
    }

private:
    std::vector<std::function<std::unique_ptr<Node>()>> node_factories_;
    std::vector<std::shared_ptr<const FaultWorkload>> faults_;
    SearchSettings settings_;
};

struct SearchProfile {
    int max_depth = kDefaultMaxDepth;
    int max_states = kDefaultMaxStates;
    uint32_t seed = kDefaultSearchSeed;
    int random_dfs_probes = kDefaultRandomDfsProbes;
};

SearchProfile searchProfile(int max_depth,
                            int max_states,
                            uint32_t seed = kDefaultSearchSeed,
                            int random_dfs_probes = kDefaultRandomDfsProbes) {
    return SearchProfile{max_depth, max_states, seed, random_dfs_probes};
}

struct TestSpec {
    std::string title;
    SearchProfile profile;
    bool internal_framework_test = false;
    ScenarioBuilder builder;
    std::vector<std::shared_ptr<const SearchTestWorkload>> workloads;
    std::vector<std::shared_ptr<const FaultWorkload>> faults;

    Scenario build() const {
        ScenarioBuilder working = builder;
        for (const auto& workload : workloads) {
            workload->setup(working);
        }
        for (const auto& fault : faults) {
            working.addFault(fault);
        }

        Scenario scenario = working.build();
        scenario.settings.test_title = title;
        scenario.settings.max_depth = profile.max_depth;
        scenario.settings.max_states = profile.max_states;
        scenario.settings.seed = profile.seed;
        scenario.settings.random_dfs_probes = profile.random_dfs_probes;
        scenario.settings.internal_framework_test = internal_framework_test;

        for (const auto& workload : workloads) {
            scenario.settings.workload_names.push_back(workload->description());
            workload->configure(scenario.settings);
        }
        return scenario;
    }
};

class TestRunner {
public:
    template <typename Fn>
    void test(const std::string& name, Fn fn) {
        try {
            fn();
            ++passed_;
            std::cout << "[PASS] " << name << std::endl;
        } catch (const std::exception& e) {
            ++failed_;
            std::cout << "[FAIL] " << name << ": " << e.what() << std::endl;
        }
    }

    void check(bool condition, const std::string& message) const {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    int finish() const {
        std::cout << "Tests: " << passed_ << " passed, " << failed_
                  << " failed" << std::endl;
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};

Workload createTableWorkload() {
    std::vector<Command> commands{
        CreateTableCommand{"people", {"id:int", "name:string"}}
    };
    return Workload(commands, buzzDBOracleResults(commands));
}

class BuzzDBClientWorkload final : public SearchTestWorkload {
public:
    BuzzDBClientWorkload(Address server,
                         Address client,
                         int client_id,
                         Workload workload)
        : server_(std::move(server)),
          client_(std::move(client)),
          client_id_(client_id),
          workload_(std::move(workload)) {}

    std::string description() const override {
        return "BuzzDBClient";
    }

    void setup(ScenarioBuilder& builder) const override {
        builder.addServer(server_, std::make_unique<BuzzDBApplication>());
        builder.addClient(client_, server_, client_id_, workload_);
    }

    void configure(SearchSettings& settings) const override {
        settings.invariants.push_back(
            Predicates::resultsOk(std::vector<Address>{client_}));
        settings.goals.push_back(
            Predicates::clientsDone(std::vector<Address>{client_}));
    }

    CheckResult check(const SearchState& state) const override {
        return clientDone(state, client_);
    }

private:
    Address server_;
    Address client_;
    int client_id_;
    Workload workload_;
};

Scenario oneClientBuzzDBScenario(Workload workload) {
    Address server = DemoAddress::server1();
    Address client = DemoAddress::client1();
    return ScenarioBuilder()
        .addServer(server, std::make_unique<BuzzDBApplication>())
        .addClient(client, server, 1, std::move(workload))
        .build();
}

std::string formatSelectAllResult(const SelectAllResult& result) {
    const auto& columns = result.columns;
    const auto& rows = result.rows;
    std::vector<size_t> widths;
    widths.reserve(columns.size());
    for (const auto& column : columns) {
        widths.push_back(column.size());
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    std::ostringstream out;
    out << "  ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i != 0) {
            out << " | ";
        }
        out << std::left << std::setw(static_cast<int>(widths[i]))
            << columns[i];
    }
    out << "\n";

    out << "  ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i != 0) {
            out << "-+-";
        }
        out << std::string(widths[i], '-');
    }
    out << "\n";

    for (const auto& row : rows) {
        out << "  ";
        for (size_t i = 0; i < row.size(); ++i) {
            if (i != 0) {
                out << " | ";
            }
            out << std::left << std::setw(static_cast<int>(widths[i]))
                << row[i];
        }
        out << "\n";
    }
    return out.str();
}

void printBuzzDBDemo() {
    std::cout << "\nDemo: local BuzzDB table" << std::endl;
    BuzzDBCore db;
    std::vector<Command> commands{
        parseSQL("CREATE TABLE people(id:int, name:string, role:string)")
    };

    std::istringstream data_file(
        "# table|id|name|role\n"
        "people|1|Ada|systems\n"
        "people|2|Grace|compilers\n");
    std::vector<Command> load_commands =
        loadTupleFileCommands(data_file, "demo.people");
    commands.insert(commands.end(), load_commands.begin(), load_commands.end());
    commands.push_back(parseSQL(
        "UPDATE people SET role=query-planning WHERE id=2"));
    commands.push_back(parseSQL("DELETE FROM people WHERE id=1"));
    commands.push_back(CountRowsCommand{"people"});

    for (const auto& command : commands) {
        Result result = db.execute(command);
        std::cout << "  " << describeCommand(command)
                  << " -> " << describeResult(result) << std::endl;
    }

    std::string sql = "PROJECT * FROM people";
    Command query = parseSQL(sql);
    Result query_result = db.execute(query);
    std::cout << "  " << sql << " -> "
              << describeResult(query_result) << std::endl;
    if (const auto* rows = std::get_if<SelectAllResult>(&query_result)) {
        std::cout << formatSelectAllResult(*rows);
    }
    std::cout << "  digest: " << db.digest() << "\n" << std::endl;
}

void printDurableBuzzDBDemo() {
    const std::string database_file = "/tmp/buzzdb-v109-buzzdb.dat";
    std::remove(database_file.c_str());

    std::cout << "Demo: reopen node-local buzzdb.dat" << std::endl;
    {
        BuzzDBCore db(database_file);
        std::cout << "  new database: "
                  << (db.isNewDatabase() ? "yes" : "no") << std::endl;
        db.execute(parseSQL(
            "CREATE TABLE people(id:int, name:string, role:string)"));
        std::istringstream data_file(
            "people|1|Ada|systems\n"
            "people|2|Grace|compilers\n");
        for (const auto& command :
             loadTupleFileCommands(data_file, "durable.demo.people")) {
            db.execute(command);
        }
        std::cout << "  storage pages after load: "
                  << db.storagePageCount() << std::endl;
    }

    BuzzDBCore reopened(database_file);
    std::cout << "  reopened database: "
              << (reopened.isNewDatabase() ? "empty" : "loaded")
              << std::endl;
    Result result = reopened.execute(parseSQL("SELECT * FROM people"));
    if (const auto* rows = std::get_if<SelectAllResult>(&result)) {
        std::cout << formatSelectAllResult(*rows);
    }
    std::cout << "  id index lookup id=2 -> row ";
    std::vector<size_t> lookup = reopened.lookupIndex("people", "id", "2");
    if (lookup.empty()) {
        std::cout << "<missing>";
    } else {
        std::cout << lookup.front();
    }
    std::cout << "\n" << std::endl;

    std::remove(database_file.c_str());
}

int main() {
    std::cout << "BuzzDB v109: BuzzDB core inside the search simulator"
              << std::endl;
    printBuzzDBDemo();
    printDurableBuzzDBDemo();
    TestRunner tests;

    Address server("server1");
    Address client("client1");

    tests.test("SearchSettings uses explicit delivery priority", [&] {
        SearchSettings settings;
        MessageEnvelope envelope{
            1,
            client,
            server,
            ClientRequest{1, 1, CreateTableCommand{"people", {"name"}}}
        };
        tests.check(settings.shouldDeliver(envelope),
                    "default network should deliver");
        settings.networkActive(false);
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive network should block delivery");
        settings.linkActive(client, server, true);
        tests.check(settings.shouldDeliver(envelope),
                    "explicit link should override network");
        settings.senderActive(client, false);
        tests.check(settings.shouldDeliver(envelope),
                    "explicit link should override sender");
        settings.resetNetwork();
        settings.senderActive(client, false);
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive sender should block delivery");
        settings.receiverActive(server, true);
        tests.check(!settings.shouldDeliver(envelope),
                    "sender should take priority over receiver");
        settings.resetNetwork();
        settings.receiverActive(server, false);
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive receiver should block delivery");
        settings.resetNetwork();
        tests.check(settings.shouldDeliver(envelope),
                    "resetNetwork should restore delivery");
    });

    tests.test("BuzzDBCore creates tables, inserts rows, and scans", [&] {
        BuzzDBCore db;
        tests.check(db.execute(CountRowsCommand{"people"}) ==
                        Result{TableNotFoundResult{}},
                    "missing table should return TableNotFound");
        tests.check(db.execute(CreateTableCommand{"bad", {"id", "id"}}) ==
                        Result{InvalidSchemaResult{}},
                    "duplicate columns should be rejected");
        tests.check(db.execute(CreateTableCommand{"people", {"name", "role"}}) ==
                        Result{CreateTableOkResult{}},
                    "create table should succeed");
        tests.check(db.execute(CreateTableCommand{"people", {"name", "role"}}) ==
                        Result{TableAlreadyExistsResult{}},
                    "duplicate create should be rejected");
        tests.check(db.execute(InsertRowCommand{"people", {"Ada", "systems"}}) ==
                        Result{InsertOkResult{}},
                    "insert should succeed");
        tests.check(db.execute(InsertRowCommand{"people", {"Grace", "compilers"}}) ==
                        Result{InsertOkResult{}},
                    "second insert should succeed");
        tests.check(db.execute(CountRowsCommand{"people"}) ==
                        Result{CountRowsResult{2}},
                    "count should reflect inserted rows");
        tests.check(db.execute(SelectAllCommand{"people"}) ==
                        Result{SelectAllResult{
                            {"name", "role"},
                            {{"Ada", "systems"}, {"Grace", "compilers"}}}},
                    "select all should preserve insertion order");
        tests.check(db.digest().find("people") != std::string::npos,
                    "database digest should include table state");
    });

    tests.test("BuzzDB operators use Schema, Field, Tuple, and Table", [&] {
        TableMap tables;
        tests.check(
            CreateTableOperator(
                tables,
                CreateTableCommand{"people", {"id:int", "name:string"}})
                    .execute() == Result{CreateTableOkResult{}},
            "create-table operator should install typed schema");

        auto table = tables.find("people");
        tests.check(table != tables.end(), "table should exist");
        tests.check(table->second.schema().columns()[0].type == INT,
                    "schema should preserve integer column type");

        std::optional<Tuple> tuple =
            table->second.schema().makeTuple({"1", "Ada"});
        tests.check(tuple.has_value(), "schema should build typed tuples");
        tests.check(tuple->fields[0]->getType() == INT,
                    "tuple should carry typed integer field");
        tests.check(tuple->fields[1]->getType() == STRING,
                    "tuple should carry typed string field");

        tests.check(
            InsertOperator(tables, InsertRowCommand{"people", {"1", "Ada"}})
                    .execute() == Result{InsertOkResult{}},
            "insert operator should write a typed tuple");
        tests.check(
            UpdateOperator(tables, UpdateRowsCommand{
                "people",
                "name",
                "Augusta",
                "id",
                "1"}).execute() == Result{UpdateRowsResult{1}},
            "update operator should rewrite matching typed rows");
        tests.check(
            InsertOperator(tables, InsertRowCommand{"people", {"bad", "Edsger"}})
                    .execute() == Result{SchemaMismatchResult{}},
            "insert operator should reject values that do not parse");
        tests.check(
            DeleteOperator(tables, DeleteRowsCommand{"people", "name", "Augusta"})
                    .execute() == Result{DeleteRowsResult{1}},
            "delete operator should remove matching typed rows");
        tests.check(
            CountOperator(tables, CountRowsCommand{"people"}).execute() ==
                Result{CountRowsResult{0}},
                    "count operator should see the deleted row");
    });

    tests.test("BuzzDB parses operator statements and tuple files", [&] {
        Command create = parseSQL(
            "CREATE TABLE people(id:int, name:string, role:string)");
        tests.check(std::holds_alternative<CreateTableCommand>(create),
                    "parser should create table commands");

        Command update = parseSQL(
            "UPDATE people SET role=query-planning WHERE id=2");
        tests.check(std::holds_alternative<UpdateRowsCommand>(update),
                    "parser should create update commands");

        Command remove = parseSQL("DELETE FROM people WHERE id=1");
        tests.check(std::holds_alternative<DeleteRowsCommand>(remove),
                    "parser should create delete commands");

        Command parsed = parseSQL("PROJECT * FROM people;");
        const auto* select = std::get_if<SelectAllCommand>(&parsed);
        tests.check(select != nullptr,
                    "SQL parser should produce a select-all command");
        tests.check(select->table == "people",
                    "SQL parser should capture the table name");

        Command lookup = parseSQL("SELECT * FROM people WHERE id=2;");
        const auto* lookup_command = std::get_if<SelectWhereCommand>(&lookup);
        tests.check(lookup_command != nullptr,
                    "SQL parser should produce an index lookup command");
        tests.check(lookup_command->table == "people" &&
                        lookup_command->column == "id" &&
                        lookup_command->value == "2",
                    "SQL parser should capture lookup predicate");

        BuzzDBCore db;
        tests.check(db.execute(create) == Result{CreateTableOkResult{}},
                    "create table should succeed");

        std::istringstream data_file(
            "# table|id|name|role\n"
            "people|1|Ada|systems\n"
            "people|2|Grace|compilers\n");
        std::vector<Command> load_commands =
            loadTupleFileCommands(data_file, "unit.people");
        tests.check(load_commands.size() == 2,
                    "tuple-file loader should create insert commands");
        for (const auto& command : load_commands) {
            tests.check(db.execute(command) == Result{InsertOkResult{}},
                        "tuple-file insert should succeed");
        }
        tests.check(db.execute(update) == Result{UpdateRowsResult{1}},
                    "parsed update should rewrite one row");
        tests.check(db.execute(remove) == Result{DeleteRowsResult{1}},
                    "parsed delete should remove one row");

        Result result = db.execute(parsed);
        tests.check(result == Result{SelectAllResult{
                        {"id", "name", "role"},
                        {{"2", "Grace", "query-planning"}}}},
                    "PROJECT * should behave like SELECT *");
        const auto* rows = std::get_if<SelectAllResult>(&result);
        tests.check(rows != nullptr &&
                        formatSelectAllResult(*rows).find("Grace") !=
                            std::string::npos,
                    "formatted select result should include row values");
        tests.check(db.execute(lookup) == Result{SelectAllResult{
                        {"id", "name", "role"},
                        {{"2", "Grace", "query-planning"}}}},
                    "SELECT * WHERE should use the lookup path");
    });

    tests.test("BuzzDB loads durable table state from buzzdb.dat", [&] {
        const std::string database_file = "/tmp/buzzdb-v109-test-buzzdb.dat";
        std::remove(database_file.c_str());

        {
            BuzzDBCore db(database_file);
            tests.check(db.durable(),
                        "file-backed core should report durable storage");
            tests.check(db.isNewDatabase(),
                        "empty buzzdb.dat should start as a new database");
            tests.check(db.execute(parseSQL(
                            "CREATE TABLE people(id:int, name:string)")) ==
                            Result{CreateTableOkResult{}},
                        "create table should persist through storage");

            std::istringstream data_file(
                "people|1|Ada\n"
                "people|2|Grace\n");
            for (const auto& command :
                 loadTupleFileCommands(data_file, "storage.people")) {
                tests.check(db.execute(command) == Result{InsertOkResult{}},
                            "tuple-file insert should persist");
            }
            tests.check(db.storagePageCount() >= 2,
                        "storage manager should allocate header and data pages");
            tests.check(db.lookupIndex("people", "id", "2") ==
                            std::vector<size_t>{1},
                        "hash index should track inserted rows");
        }

        {
            BuzzDBCore reopened(database_file);
            tests.check(!reopened.isNewDatabase(),
                        "reopened buzzdb.dat should load existing tables");
            tests.check(reopened.execute(parseSQL("SELECT * FROM people")) ==
                            Result{SelectAllResult{
                                {"id", "name"},
                                {{"1", "Ada"}, {"2", "Grace"}}}},
                        "reopened database should return durable rows");
            tests.check(reopened.execute(parseSQL(
                            "SELECT * FROM people WHERE id=2")) ==
                            Result{SelectAllResult{
                                {"id", "name"},
                                {{"2", "Grace"}}}},
                        "durable lookup should use the rebuilt hash index");
            tests.check(reopened.lookupIndex("people", "name", "Grace") ==
                            std::vector<size_t>{1},
                        "hash index should rebuild from loaded storage");
            tests.check(reopened.bufferStats().page_loads > 0,
                        "buffer manager should read pages during reopen");
        }

        std::remove(database_file.c_str());
    });

    tests.test("Catalog and TableHeap store metadata and slotted records", [&] {
        const std::string database_file =
            "/tmp/buzzdb-v109-catalog-heap-test.dat";
        std::remove(database_file.c_str());
        RecordID second_record;

        {
            BufferManager buffer(database_file, 2);
            Catalog catalog(buffer);
            catalog.bootstrap();
            tests.check(catalog.isNewDatabase(),
                        "catalog should bootstrap an empty database");
            TableMetadata& metadata = catalog.createTable(
                "people",
                Schema::fromColumnSpecs({"id:int", "name:string"}));
            tests.check(metadata.table_id == FIRST_USER_TABLE_ID,
                        "first user table should get the first user id");
            tests.check(metadata.first_page != INVALID_PAGE_ID &&
                            metadata.first_page == metadata.last_page,
                        "new table metadata should name its first heap page");

            TableHeap heap(metadata, buffer);
            std::optional<Tuple> first = metadata.schema.makeTuple({"1", "Ada"});
            std::optional<Tuple> second =
                metadata.schema.makeTuple({"2", "Grace"});
            tests.check(first.has_value() && second.has_value(),
                        "schema should build heap tuples");
            std::optional<RecordID> first_record =
                heap.insert(std::move(*first));
            std::optional<RecordID> inserted_second =
                heap.insert(std::move(*second));
            tests.check(first_record.has_value() &&
                            inserted_second.has_value(),
                        "heap should insert tuples into slotted records");
            tests.check(first_record->page_id == metadata.first_page &&
                            first_record->slot_id == 0,
                        "first tuple should occupy slot 0 on the heap page");
            second_record = *inserted_second;
            tests.check(second_record.page_id == metadata.first_page &&
                            second_record.slot_id == 1,
                        "second tuple should occupy slot 1 on the heap page");
            tests.check(metadata.row_count == 2,
                        "table metadata should track heap row count");
            catalog.persistTableMetadata();
        }

        {
            BufferManager buffer(database_file, 2);
            Catalog catalog(buffer);
            catalog.bootstrap();
            TableMetadata& metadata = catalog.getTable("people");
            tests.check(metadata.row_count == 2 &&
                            metadata.page_ids.size() == 1,
                        "catalog should reload table metadata");
            TableHeap heap(metadata, buffer);
            std::vector<Tuple> rows = heap.readAllTuples();
            tests.check(rows.size() == 2 &&
                            rows[1].toStrings() ==
                                std::vector<std::string>{"2", "Grace"},
                        "table heap should reload slotted tuple records");
            HashIndex index;
            index.rebuild(catalog, buffer);
            tests.check(index.lookupRecords("people", "id", "2") ==
                            std::vector<RecordID>{second_record},
                        "hash index should point at the physical record id");
        }

        std::remove(database_file.c_str());
    });

    return tests.finish();
}
