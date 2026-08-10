#include "test_harness.h"
#include "svg.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {

TintaTestContext *test_context = nullptr;

void check(bool condition, const char *message) {
    test_context->check(condition, message);
}

void check_size(const char *source, float width, float height,
                const char *message) {
    TintaSvgInfo info{};
    bool ok = tinta_svg_prepare_source(
        source, std::strlen(source), 1024 * 1024, &info);
    check(ok && std::fabs(info.width - width) < 0.01f &&
              std::fabs(info.height - height) < 0.01f, message);
}

}  // namespace

void run_svg_tests(TintaTestContext &tests) {
    test_context = &tests;
    check(tinta_svg_uri_candidate("image.svg?theme=dark#icon"),
          "SVG extension with query and fragment");
    check(tinta_svg_uri_candidate("DATA:IMAGE/SVG+XML,%3Csvg%3E%3C/svg%3E"),
          "case-insensitive SVG Data URI");
    check(!tinta_svg_uri_candidate("image.svg.png"),
          "non-SVG extension rejected");
    check_size("<svg width=\"120\" height=\"60\"></svg>", 120, 60,
               "explicit SVG size");
    check_size("\xef\xbb\xbf<?xml version=\"1.0\"?><svg viewBox=\"0 0 80 40\"></svg>",
               80, 40, "BOM, XML declaration, and viewBox");
    check_size("<svg width=\"2in\" viewBox=\"0 0 4 2\"></svg>",
               192, 96, "single dimension inferred from viewBox");
    check_size("<svg></svg>", 300, 150, "default SVG size");

    TintaSvgInfo info{};
    static const char dtd[] = "<!DOCTYPE svg><svg></svg>";
    static const char entity[] = "<!ENTITY x 'x'><svg></svg>";
    static const char fake[] = "<svgx></svgx>";
    check(!tinta_svg_prepare_source(dtd, sizeof(dtd) - 1, 1024, &info),
          "DTD rejected");
    check(!tinta_svg_prepare_source(
              entity, sizeof(entity) - 1, 1024, &info),
          "entity declaration rejected");
    check(!tinta_svg_prepare_source(fake, sizeof(fake) - 1, 1024, &info),
          "SVG-like root rejected");

    TintaStr8 decoded{};
    check(tinta_svg_decode_data_uri(
              "data:image/svg+xml;charset=utf-8,%3Csvg%20width%3D%2720%27%20height%3D%2710%27%3E%3C/svg%3E",
              1024, &decoded, &info) && decoded.len > 0 &&
              info.width == 20 && info.height == 10,
          "percent-encoded Data URI");
    tinta_str8_destroy(&decoded);
    check(tinta_svg_decode_data_uri(
              "data:image/svg+xml;base64,PHN2ZyB3aWR0aD0nMzAnIGhlaWdodD0nMTUnPjwvc3ZnPg==",
              1024, &decoded, &info) && info.width == 30 && info.height == 15,
          "base64 Data URI");
    tinta_str8_destroy(&decoded);
    check(!tinta_svg_decode_data_uri(
              "data:image/png,%3Csvg%3E%3C/svg%3E", 1024,
              &decoded, &info),
          "non-SVG Data URI rejected");
    check(!tinta_svg_decode_data_uri(
              "data:image/svg+xml;foo=bar,%3Csvg%3E%3C/svg%3E", 1024,
              &decoded, &info),
          "unknown SVG Data URI metadata rejected");
    check(!tinta_svg_decode_data_uri(
              "data:image/svg+xml,%3Csvg%3E%3C/svg%3E", 4,
              &decoded, &info),
          "Data URI byte limit enforced");

}
