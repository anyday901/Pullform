#pragma once

#include "core/document/result.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

/// Display/input length units. The model always stores meters; units are
/// converted only at input and display boundaries.
enum class LengthUnit { Millimeter, Centimeter, Meter, Inch, Foot };

inline constexpr std::array<LengthUnit, 5> kAllLengthUnits = {
    LengthUnit::Millimeter, LengthUnit::Centimeter, LengthUnit::Meter, LengthUnit::Inch, LengthUnit::Foot};

double metersPerUnit(LengthUnit unit);
/// Short suffix used in files and display: "mm", "cm", "m", "in", "ft".
std::string_view unitSuffix(LengthUnit unit);
std::optional<LengthUnit> unitFromSuffix(std::string_view suffix);

/// Shown with every rejected length so the user knows what is accepted.
inline constexpr std::string_view kLengthFormatHint =
    "Enter a number with an optional unit, for example 2000, 2000mm, 200cm, 2m, 78.74in or 6.5ft.";

/// Parses a length such as "2000", "2000 mm", "2m", "78.74in", "6.5'" into meters.
/// Unitless values are interpreted in `defaultUnit` (the document's display unit,
/// millimeters by default). Accepted suffixes: mm, cm, m, in or ", ft or '.
/// Case-insensitive. Non-finite values, compound feet-inches and comma decimals
/// are rejected with a specific message.
Expected<double> parseLength(std::string_view text, LengthUnit defaultUnit);

/// Parses `count` lengths separated by commas or semicolons, e.g. "2000, 1000".
Expected<std::vector<double>> parseLengthList(std::string_view text, std::size_t count, LengthUnit defaultUnit);

/// Parses an angle in degrees ("45", "-90", "22.5deg", "30°") into radians.
Expected<double> parseAngle(std::string_view text);

/// Formats radians as degrees, trimming insignificant zeros ("45°", "22.5°").
std::string formatAngle(double radians);

/// Parses scale factors: one value for a uniform scale ("1.5", "150%") or three
/// for width, depth and height ("2, 1, 0.5"). Always returns three factors.
/// Factors must not be zero; a negative factor mirrors.
Expected<std::vector<double>> parseScaleFactors(std::string_view text);

double metersTo(double meters, LengthUnit unit);

/// Formats meters in `unit`, trimming insignificant zeros ("750 mm", "0.75 m").
std::string formatLength(double meters, LengthUnit unit, bool withUnit = true);

} // namespace pf
