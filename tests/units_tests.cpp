#include "helpers.hpp"

#include "core/document/units.hpp"

using namespace pf;
using pf::test::near;

TEST_CASE("equivalent inputs convert to the same meters") {
    const auto u = LengthUnit::Millimeter;
    // Scenario 8: 2000 mm and 2 m are the same length.
    CHECK(near(*parseLength("2000", u), 2.0));
    CHECK(near(*parseLength("2000mm", u), 2.0));
    CHECK(near(*parseLength("2000 mm", u), 2.0));
    CHECK(near(*parseLength("200cm", u), 2.0));
    CHECK(near(*parseLength("2m", u), 2.0));
    CHECK(near(*parseLength("2 M", u), 2.0));
    CHECK(near(*parseLength("78.74in", u), 78.74 * 0.0254));
    CHECK(near(*parseLength("10\"", u), 0.254));
    CHECK(near(*parseLength("6.5ft", u), 6.5 * 0.3048));
    CHECK(near(*parseLength("3'", u), 3 * 0.3048));
    CHECK(near(*parseLength(".5m", u), 0.5));
    CHECK(near(*parseLength("1e3", u), 1.0));
    CHECK(near(*parseLength("  750  ", u), 0.75));
    CHECK(near(*parseLength("-100", u), -0.1));
}

TEST_CASE("unitless values use the display unit") {
    CHECK(near(*parseLength("2", LengthUnit::Meter), 2.0));
    CHECK(near(*parseLength("2", LengthUnit::Centimeter), 0.02));
    CHECK(near(*parseLength("12", LengthUnit::Inch), 0.3048));
}

TEST_CASE("malformed and non-finite lengths are rejected with a useful message") {
    const auto u = LengthUnit::Millimeter;
    for (const char* bad : {"", "   ", "abc", "2 meters", "2km", "1.2.3", "--2", "e5", "m", "1e", "2 m m"}) {
        CAPTURE(bad);
        const auto r = parseLength(bad, u);
        REQUIRE_FALSE(r.ok());
        CHECK_FALSE(r.error().empty());
    }
    CHECK(parseLength("inf", u).error().find("finite") != std::string::npos);
    CHECK(parseLength("-Infinity", u).error().find("finite") != std::string::npos);
    CHECK(parseLength("NaN", u).error().find("finite") != std::string::npos);
    CHECK(parseLength("1e999", u).error().find("finite") != std::string::npos);
    CHECK(parseLength("6'2\"", u).error().find("not supported") != std::string::npos);
    CHECK(parseLength("2,5", u).error().find("decimal separator") != std::string::npos);
    CHECK(parseLength("abc", u).error().find("for example") != std::string::npos);
}

TEST_CASE("length lists") {
    const auto r = parseLengthList("2000, 1000", 2, LengthUnit::Millimeter);
    REQUIRE(r.ok());
    CHECK(near((*r)[0], 2.0));
    CHECK(near((*r)[1], 1.0));
    const auto mixed = parseLengthList("2m;100cm", 2, LengthUnit::Millimeter);
    REQUIRE(mixed.ok());
    CHECK(near((*mixed)[1], 1.0));
    CHECK_FALSE(parseLengthList("2000", 2, LengthUnit::Millimeter).ok());
    CHECK_FALSE(parseLengthList("1,2,3", 2, LengthUnit::Millimeter).ok());
    CHECK_FALSE(parseLengthList("2000, x", 2, LengthUnit::Millimeter).ok());
}

TEST_CASE("formatting trims zeros and converts units") {
    CHECK(formatLength(0.75, LengthUnit::Millimeter) == "750 mm");
    CHECK(formatLength(0.75, LengthUnit::Meter) == "0.75 m");
    CHECK(formatLength(0.0254, LengthUnit::Inch) == "1 in");
    CHECK(formatLength(1.23456, LengthUnit::Centimeter, false) == "123.456");
    CHECK(formatLength(-0.000001, LengthUnit::Millimeter) == "0 mm");
}
