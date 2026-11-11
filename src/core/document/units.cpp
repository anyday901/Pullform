#include "core/document/units.hpp"

#include <cctype>
#include <cmath>
#include <locale>
#include <numbers>
#include <sstream>

namespace pf {

double metersPerUnit(LengthUnit unit) {
    switch (unit) {
    case LengthUnit::Millimeter: return 0.001;
    case LengthUnit::Centimeter: return 0.01;
    case LengthUnit::Meter: return 1.0;
    case LengthUnit::Inch: return 0.0254;
    case LengthUnit::Foot: return 0.3048;
    }
    return 1.0;
}

std::string_view unitSuffix(LengthUnit unit) {
    switch (unit) {
    case LengthUnit::Millimeter: return "mm";
    case LengthUnit::Centimeter: return "cm";
    case LengthUnit::Meter: return "m";
    case LengthUnit::Inch: return "in";
    case LengthUnit::Foot: return "ft";
    }
    return "m";
}

std::optional<LengthUnit> unitFromSuffix(std::string_view suffix) {
    for (LengthUnit u : kAllLengthUnits) {
        if (unitSuffix(u) == suffix) return u;
    }
    if (suffix == "\"") return LengthUnit::Inch;
    if (suffix == "'") return LengthUnit::Foot;
    return std::nullopt;
}

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

Error unrecognized(std::string_view text) {
    return Error{"\"" + std::string(text) + "\" is not a recognized length. " + std::string(kLengthFormatHint)};
}

// Locale-independent decimal conversion ("." is always the separator).
std::optional<double> toDouble(const std::string& number) {
    std::istringstream in(number);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail() || !in.eof()) return std::nullopt;
    return v;
}

} // namespace

Expected<double> parseLength(std::string_view input, LengthUnit defaultUnit) {
    const std::string_view text = trim(input);
    if (text.empty()) return Error{"Enter a length."};
    const std::string s = lower(text);

    std::size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    if (s.compare(i, 3, "inf") == 0 || s.compare(i, 3, "nan") == 0) {
        return Error{"Length must be a finite number."};
    }
    for (std::size_t k = 1; k + 1 < s.size(); ++k) {
        if (s[k] == ',' && isDigit(s[k - 1]) && isDigit(s[k + 1])) {
            return Error{"Use \".\" as the decimal separator. " + std::string(kLengthFormatHint)};
        }
    }
    {
        // 6'2" style compound feet-and-inches: digit, optional spaces, ', optional spaces, digit.
        const auto q = s.find('\'');
        if (q != std::string::npos && q > 0) {
            std::size_t before = q;
            while (before > 0 && isSpace(s[before - 1])) --before;
            std::size_t after = q + 1;
            while (after < s.size() && isSpace(s[after])) ++after;
            if (before > 0 && isDigit(s[before - 1]) && after < s.size() && isDigit(s[after])) {
                return Error{"Compound feet-and-inches like 6'2\" is not supported yet. " +
                             std::string(kLengthFormatHint)};
            }
        }
    }

    // number: [+-]? (digits [. digits?] | . digits) ([e] [+-]? digits)?
    const std::size_t numberStart = 0;
    std::size_t intDigits = 0, fracDigits = 0;
    while (i < s.size() && isDigit(s[i])) { ++i; ++intDigits; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && isDigit(s[i])) { ++i; ++fracDigits; }
    }
    if (intDigits == 0 && fracDigits == 0) return unrecognized(text);
    if (i < s.size() && s[i] == 'e') {
        std::size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
        std::size_t expDigits = 0;
        while (j < s.size() && isDigit(s[j])) { ++j; ++expDigits; }
        if (expDigits == 0) return unrecognized(text);
        i = j;
    }
    std::string number = s.substr(numberStart, i - numberStart);
    if (!number.empty() && number.back() == '.') number.pop_back();
    if (!number.empty() && number.front() == '+') number.erase(0, 1);

    while (i < s.size() && isSpace(s[i])) ++i;
    const std::string suffix = s.substr(i);
    LengthUnit unit = defaultUnit;
    if (!suffix.empty()) {
        const auto parsed = unitFromSuffix(suffix);
        if (!parsed) return Error{"Unknown unit in \"" + std::string(text) + "\". " + std::string(kLengthFormatHint)};
        unit = *parsed;
    }

    const auto value = toDouble(number);
    if (!value) {
        // Only out-of-range values (e.g. 1e999) fail to convert after the checks above.
        return Error{"Length must be a finite number."};
    }
    const double meters = *value * metersPerUnit(unit);
    if (!std::isfinite(meters)) return Error{"Length must be a finite number."};
    return meters;
}

Expected<std::vector<double>> parseLengthList(std::string_view text, std::size_t count, LengthUnit defaultUnit) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t k = 0; k <= text.size(); ++k) {
        if (k == text.size() || text[k] == ',' || text[k] == ';') {
            const auto part = trim(text.substr(start, k - start));
            if (!part.empty()) parts.push_back(part);
            start = k + 1;
        }
    }
    if (parts.size() != count) {
        if (count == 2) return Error{"Enter width and depth separated by a comma, for example 2000, 1000."};
        return Error{"Enter " + std::to_string(count) + " values separated by commas."};
    }
    std::vector<double> values;
    for (auto part : parts) {
        auto r = parseLength(part, defaultUnit);
        if (!r) return Error{r.error()};
        values.push_back(*r);
    }
    return values;
}

namespace {

constexpr double kPi = std::numbers::pi;

/// Advances `i` past an optional sign.
void skipSign(const std::string& s, std::size_t& i) {
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
}

/// Advances `i` past decimal digits and returns how many there were.
std::size_t skipDigits(const std::string& s, std::size_t& i) {
    const std::size_t start = i;
    while (i < s.size() && isDigit(s[i])) ++i;
    return i - start;
}

/// Advances `i` past an exponent ("e", optional sign, digits). Returns false
/// for an "e" without digits; true when there is no exponent or a valid one.
bool skipExponent(const std::string& s, std::size_t& i) {
    if (i >= s.size() || s[i] != 'e') return true;
    std::size_t j = i + 1;
    skipSign(s, j);
    if (skipDigits(s, j) == 0) return false;
    i = j;
    return true;
}

/// A plain decimal number, [+-]? (digits [. digits?] | . digits) (e [+-]? digits)?,
/// with nothing else around it.
std::optional<double> plainNumber(std::string_view text) {
    const std::string s = lower(trim(text));
    std::size_t i = 0;
    skipSign(s, i);
    std::size_t digits = skipDigits(s, i);
    if (i < s.size() && s[i] == '.') {
        ++i;
        digits += skipDigits(s, i);
    }
    if (digits == 0 || !skipExponent(s, i) || i != s.size()) return std::nullopt;
    std::string number = s;
    if (number.front() == '+') number.erase(0, 1);
    if (!number.empty() && number.back() == '.') number.pop_back();
    const auto v = toDouble(number);
    if (!v || !std::isfinite(*v)) return std::nullopt;
    return v;
}

bool removeSuffix(std::string_view& s, std::string_view suffix) {
    if (!s.ends_with(suffix)) return false;
    s.remove_suffix(suffix.size());
    s = trim(s);
    return true;
}

} // namespace

Expected<double> parseAngle(std::string_view input) {
    std::string_view text = trim(input);
    if (text.empty()) return Error{"Enter an angle in degrees."};
    const std::string low = lower(text);
    std::string_view s = low;
    if (!removeSuffix(s, "°") && !removeSuffix(s, "degrees")) removeSuffix(s, "deg");
    const auto v = plainNumber(s);
    if (!v.has_value()) {
        return Error{"\"" + std::string(text) + "\" is not a recognized angle. Enter degrees, for example 90, -45 or 22.5."};
    }
    return *v * kPi / 180.0;
}

std::optional<Expected<ArrayCount>> parseArrayCount(std::string_view input) {
    const std::string low = lower(trim(input));
    std::string_view s = low;
    ArrayCount result;
    if (s.starts_with('/')) {
        result.divide = true;
        s.remove_prefix(1);
    } else if (s.starts_with('*') || s.starts_with('x')) {
        s.remove_prefix(1);
    } else if (s.ends_with('*') || s.ends_with('x')) {
        s.remove_suffix(1);
    } else {
        return std::nullopt;
    }
    s = trim(s);
    const auto v = plainNumber(s);
    if (!v) {
        // "x" on its own, or letters after it ("xyz"): not array syntax.
        if (s.empty() || !isDigit(s.front())) return std::nullopt;
        return Expected<ArrayCount>{Error{"\"" + std::string(trim(input)) + "\" is not a recognized number of copies."}};
    }
    if (*v != std::floor(*v) || *v < 1.0 || *v > kMaxArrayCount) {
        return Expected<ArrayCount>{Error{"The number of copies must be a whole number from 1 to " + std::to_string(kMaxArrayCount) +
                                          "."}};
    }
    result.count = static_cast<int>(*v);
    return Expected<ArrayCount>{result};
}

std::string formatAngle(double radians) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.setf(std::ios::fixed);
    out.precision(2);
    out << radians * 180.0 / kPi;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    if (text == "-0") text = "0";
    return text + "°";
}

Expected<std::vector<double>> parseScaleFactors(std::string_view text) {
    const auto hint = [] {
        return Error{"Enter one scale factor (for example 2, 0.5 or 150%) or three for width, depth and height "
                     "(for example 2, 1, 1)."};
    };
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t k = 0; k <= text.size(); ++k) {
        if (k == text.size() || text[k] == ',' || text[k] == ';') {
            parts.push_back(trim(text.substr(start, k - start)));
            start = k + 1;
        }
    }
    if (parts.size() != 1 && parts.size() != 3) return hint();
    std::vector<double> factors;
    for (auto part : parts) {
        const bool percent = removeSuffix(part, "%");
        auto v = plainNumber(part);
        if (!v.has_value()) return hint();
        if (percent) *v /= 100.0;
        if (!(*v > 0.0)) return Error{"Scale factors must be positive. Mirroring (negative factors) is not supported."};
        factors.push_back(*v);
    }
    if (factors.size() == 1) factors = {factors[0], factors[0], factors[0]};
    return factors;
}

double metersTo(double meters, LengthUnit unit) { return meters / metersPerUnit(unit); }

std::string formatLength(double meters, LengthUnit unit, bool withUnit) {
    const double value = metersTo(meters, unit);
    const int decimals = unit == LengthUnit::Millimeter ? 2 : unit == LengthUnit::Centimeter ? 3 : 4;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.setf(std::ios::fixed);
    out.precision(decimals);
    out << value;
    std::string text = out.str();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text == "-0") text = "0";
    if (withUnit) {
        text += ' ';
        text += unitSuffix(unit);
    }
    return text;
}

} // namespace pf
