#pragma once

// A compatible subset of Arduino's WString.h `String` class, backed by std::string.
// Scoped to exactly the methods/overloads the vendored zimodem sketch actually calls
// (verified by grepping every .ino/.h in external/zimodem/zimodem) -- not a guess at
// the full Arduino String API. If a future compile error shows a real need for more
// (e.g. toInt(), reserve(), setCharAt()), add it then rather than speculatively now.

#include <cctype>
#include <cstddef>
#include <string>

class String
{
public:
    String() = default;
    String(const String&) = default;
    String(String&&) noexcept = default;
    String(const char* cstr) : value_(cstr ? cstr : "") {}
    String(char c) : value_(1, c) {}

    // Arduino's real String(int)/String(long)/etc. constructors are implicit; every
    // call site in zimodem that builds a String from a number does so via an explicit
    // `String(x)` call (verified by grep), so these are marked explicit to catch any
    // accidental implicit numeric->String conversion at compile time. Loosen if a real
    // one turns up.
    explicit String(int value) : value_(std::to_string(value)) {}
    explicit String(unsigned int value) : value_(std::to_string(value)) {}
    explicit String(long value) : value_(std::to_string(value)) {}
    explicit String(unsigned long value) : value_(std::to_string(value)) {}

    String& operator=(const String&) = default;
    String& operator=(String&&) noexcept = default;
    String& operator=(const char* cstr)
    {
        value_ = cstr ? cstr : "";
        return *this;
    }

    String& operator+=(const String& other)
    {
        value_ += other.value_;
        return *this;
    }
    String& operator+=(const char* cstr)
    {
        if (cstr)
            value_ += cstr;
        return *this;
    }
    String& operator+=(char c)
    {
        value_ += c;
        return *this;
    }

    bool concat(const String& s) { *this += s; return true; }
    bool concat(const char* s) { *this += s; return true; }
    bool concat(char c) { *this += c; return true; }

    const char* c_str() const { return value_.c_str(); }
    unsigned int length() const { return static_cast<unsigned int>(value_.size()); }

    char operator[](unsigned int index) const
    {
        return index < value_.size() ? value_[index] : '\0';
    }
    // Real Arduino String::operator[] returns a mutable reference (zcommand.ino's
    // getNextSerialCommand does `currentCommand[i]=petToAsc(currentCommand[i]);`), safe
    // for out-of-range indices by returning a scratch cell rather than undefined
    // behavior -- matching real Arduino's own bounds-checked-with-dummy-buffer approach.
    char& operator[](unsigned int index)
    {
        static char outOfRange;
        outOfRange = '\0';
        return index < value_.size() ? value_[index] : outOfRange;
    }

    bool operator==(const String& other) const { return value_ == other.value_; }
    bool operator==(const char* cstr) const { return value_ == (cstr ? cstr : ""); }
    bool operator!=(const String& other) const { return !(*this == other); }
    bool operator!=(const char* cstr) const { return !(*this == cstr); }

    bool equals(const String& s) const { return *this == s; }
    bool equalsIgnoreCase(const String& s) const
    {
        return case_insensitive_equal(value_, s.value_);
    }

    bool startsWith(const String& prefix) const
    {
        return value_.size() >= prefix.value_.size() &&
               value_.compare(0, prefix.value_.size(), prefix.value_) == 0;
    }
    bool endsWith(const String& suffix) const
    {
        return value_.size() >= suffix.value_.size() &&
               value_.compare(value_.size() - suffix.value_.size(), suffix.value_.size(), suffix.value_) == 0;
    }

    int indexOf(char c, unsigned int fromIndex = 0) const
    {
        return find_to_int(value_.find(c, fromIndex));
    }
    int indexOf(const String& s, unsigned int fromIndex = 0) const
    {
        return find_to_int(value_.find(s.value_, fromIndex));
    }
    int lastIndexOf(char c) const { return find_to_int(value_.rfind(c)); }
    int lastIndexOf(const String& s) const { return find_to_int(value_.rfind(s.value_)); }

    // Matches Arduino semantics: characters from `left` up to (but not including) `right`.
    String substring(unsigned int left, unsigned int right) const
    {
        if (left >= value_.size() || left >= right)
            return String();
        right = right > value_.size() ? static_cast<unsigned int>(value_.size()) : right;
        return String(value_.substr(left, right - left).c_str());
    }
    String substring(unsigned int left) const
    {
        return substring(left, static_cast<unsigned int>(value_.size()));
    }

    void remove(unsigned int index)
    {
        if (index < value_.size())
            value_.erase(index);
    }
    void remove(unsigned int index, unsigned int count)
    {
        if (index < value_.size())
            value_.erase(index, count);
    }

    void replace(char find, char rep)
    {
        for (auto& ch : value_)
            if (ch == find)
                ch = rep;
    }
    void replace(const String& find, const String& rep)
    {
        if (find.value_.empty())
            return;
        std::size_t pos = 0;
        while ((pos = value_.find(find.value_, pos)) != std::string::npos)
        {
            value_.replace(pos, find.value_.size(), rep.value_);
            pos += rep.value_.size();
        }
    }

    void trim()
    {
        std::size_t start = value_.find_first_not_of(" \t\r\n");
        std::size_t end = value_.find_last_not_of(" \t\r\n");
        value_ = (start == std::string::npos) ? std::string() : value_.substr(start, end - start + 1);
    }
    void toLowerCase() { for (auto& c : value_) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    void toUpperCase() { for (auto& c : value_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }

private:
    std::string value_;

    static int find_to_int(std::size_t pos) { return pos == std::string::npos ? -1 : static_cast<int>(pos); }
    static bool case_insensitive_equal(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); i++)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    }
};

inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, char b) { String r(a); r += b; return r; }
inline String operator+(char a, const String& b) { String r(a); r += b; return r; }
