#include "test_harness.h"

#include <md4c.h>
#include <md4c-test.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Event {
    char category;
    int type;
    MD_SOURCE_RANGE source;
    std::string text;
    MD_SOURCE_RANGE content{MD_OFFSET_INVALID, MD_OFFSET_INVALID};
    int value = 0;
};

struct Recorder {
    std::vector<Event> events;
    std::vector<int> blocks;
    std::vector<int> spans;
    int abort_after = -1;
    char abort_category = 0;
    int abort_code = 7;
    bool did_abort = false;
    int calls_after_abort = 0;
    int calls = 0;
};

MD_SOURCE_RANGE copy_range(const MD_SOURCE_RANGE *source) {
    return source ? *source : MD_SOURCE_RANGE{MD_OFFSET_INVALID,
                                               MD_OFFSET_INVALID};
}

int maybe_abort(Recorder *recorder, char category) {
    recorder->calls++;
    if (recorder->did_abort) {
        recorder->calls_after_abort++;
        return recorder->abort_code;
    }
    if (recorder->abort_category == category) {
        recorder->did_abort = true;
        return recorder->abort_code;
    }
    return recorder->abort_after >= 0 &&
                   recorder->calls >= recorder->abort_after
               ? 7
               : 0;
}

int enter_block(MD_BLOCKTYPE type, void *detail,
                const MD_SOURCE_RANGE *source, void *userdata) {
    auto *recorder = static_cast<Recorder *>(userdata);
    Event event{'B', static_cast<int>(type), copy_range(source)};
    if (type == MD_BLOCK_DETAILS)
        event.value = static_cast<MD_BLOCK_DETAILS_DETAIL *>(detail)->is_open;
    else if (type == MD_BLOCK_CODE) {
        auto *code = static_cast<MD_BLOCK_CODE_DETAIL *>(detail);
        event.text.assign(code->lang.text ? code->lang.text : "",
                          code->lang.size);
    }
    recorder->events.push_back(event);
    recorder->blocks.push_back(static_cast<int>(type));
    return maybe_abort(recorder, 'B');
}

int leave_block(MD_BLOCKTYPE type, void *, const MD_SOURCE_RANGE *source,
                void *userdata) {
    auto *recorder = static_cast<Recorder *>(userdata);
    recorder->events.push_back(
        Event{'b', static_cast<int>(type), copy_range(source)});
    if (!recorder->blocks.empty() && recorder->blocks.back() == type)
        recorder->blocks.pop_back();
    return maybe_abort(recorder, 'b');
}

int enter_span(MD_SPANTYPE type, void *detail,
               const MD_SOURCE_RANGE *source, void *userdata) {
    auto *recorder = static_cast<Recorder *>(userdata);
    Event event{'S', static_cast<int>(type), copy_range(source)};
    if (type == MD_SPAN_LATEXMATH || type == MD_SPAN_LATEXMATH_DISPLAY) {
        auto *math = static_cast<MD_SPAN_LATEXMATH_DETAIL *>(detail);
        event.content = math->content;
        event.value = static_cast<int>(math->delimiter);
    } else if (type == MD_SPAN_A) {
        auto *link = static_cast<MD_SPAN_A_DETAIL *>(detail);
        event.text.assign(link->href.text ? link->href.text : "",
                          link->href.size);
        event.text.push_back('|');
        event.text.append(link->title.text ? link->title.text : "",
                          link->title.size);
    }
    recorder->events.push_back(event);
    recorder->spans.push_back(static_cast<int>(type));
    return maybe_abort(recorder, 'S');
}

int leave_span(MD_SPANTYPE type, void *, const MD_SOURCE_RANGE *source,
               void *userdata) {
    auto *recorder = static_cast<Recorder *>(userdata);
    recorder->events.push_back(
        Event{'s', static_cast<int>(type), copy_range(source)});
    if (!recorder->spans.empty() && recorder->spans.back() == type)
        recorder->spans.pop_back();
    return maybe_abort(recorder, 's');
}

int text_callback(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                  const MD_SOURCE_RANGE *source, void *userdata) {
    auto *recorder = static_cast<Recorder *>(userdata);
    recorder->events.push_back(Event{'T', static_cast<int>(type),
                                     copy_range(source),
                                     std::string(text, text + size)});
    return maybe_abort(recorder, 'T');
}

MD_PARSER parser() {
    MD_PARSER value{};
    value.abi_version = MD_PARSER_ABI_VERSION;
    value.flags = MD_FLAG_TABLES | MD_FLAG_TASKLISTS |
                  MD_FLAG_LATEXMATHSPANS | MD_FLAG_TINTA_HTML |
                  MD_FLAG_HIGHLIGHT | MD_FLAG_STRIKETHROUGH |
                  MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS;
    value.enter_block = enter_block;
    value.leave_block = leave_block;
    value.enter_span = enter_span;
    value.leave_span = leave_span;
    value.text = text_callback;
    return value;
}

const Event *find_event(const Recorder &recorder, char category, int type,
                        size_t ordinal = 0) {
    for (const auto &event : recorder.events) {
        if (event.category == category && event.type == type) {
            if (ordinal == 0) return &event;
            ordinal--;
        }
    }
    return nullptr;
}

std::string joined_text(const Recorder &recorder) {
    std::string result;
    for (const auto &event : recorder.events)
        if (event.category == 'T') result += event.text;
    return result;
}

size_t count_event(const Recorder &recorder, char category, int type) {
    return static_cast<size_t>(std::count_if(
        recorder.events.begin(), recorder.events.end(),
        [&](const Event &event) {
            return event.category == category && event.type == type;
        }));
}

bool event_has_source(const Recorder &recorder, char category, int type,
                      const std::string &input, const char *expected,
                      size_t ordinal = 0) {
    const Event *event = find_event(recorder, category, type, ordinal);
    if (!event || event->source.begin == MD_OFFSET_INVALID ||
        event->source.end < event->source.begin ||
        event->source.end > input.size())
        return false;
    return input.substr(event->source.begin,
                        event->source.end - event->source.begin) == expected;
}

bool text_has_source(const Recorder &recorder, const std::string &input,
                     const char *expected_source, const char *expected_text) {
    for (const auto &event : recorder.events) {
        if (event.category != 'T' || event.source.begin == MD_OFFSET_INVALID ||
            event.source.end < event.source.begin ||
            event.source.end > input.size())
            continue;
        if (input.substr(event.source.begin,
                         event.source.end - event.source.begin) ==
                expected_source &&
            event.text == expected_text)
            return true;
    }
    return false;
}

}  // namespace

void run_md4c_tests(TintaTestContext &tests) {
    auto check = [&](bool condition, const char *message) {
        tests.check(condition, message);
    };

    std::string input =
        "# H\xC3\xA9\r\n\r\n"
        "- *a* [b](u&amp;v \"t&#x21;\")\r\n\r\n"
        "```c&amp;p\r\n&amp; raw\r\n```\r\n\r\n"
        "$$x\r\n+ y$$\r\n\r\n"
        "<details title='x > y' open><summary>S &amp; T "
        "<a href='q&amp;r' title='&#65;'>L</a></summary>"
        "<p>B</p></details>\r\n\r\n"
        "E: &#x1F600; &NotEqualTilde; &#0; &bogus; ";
    input.push_back('\0');
    input += " Z\n";

    Recorder recorder;
    auto value = parser();
    int status = md_parse(input.data(), static_cast<MD_SIZE>(input.size()),
                          &value, &recorder);
    check(status == 0, "md4c ABI 1 parser succeeds");
    check(recorder.blocks.empty() && recorder.spans.empty(),
          "md4c events remain balanced");

    const Event *doc = find_event(recorder, 'B', MD_BLOCK_DOC);
    check(doc && doc->source.begin == 0 && doc->source.end == input.size(),
          "document range covers the original input");
    const Event *heading = find_event(recorder, 'B', MD_BLOCK_H);
    check(heading && heading->source.begin == 0 &&
              input.substr(heading->source.begin,
                           heading->source.end - heading->source.begin) ==
                  "# H\xC3\xA9",
          "heading range includes its delimiter and excludes CRLF");
    const Event *list = find_event(recorder, 'B', MD_BLOCK_UL);
    const Event *item = find_event(recorder, 'B', MD_BLOCK_LI);
    check(list && item && list->source.begin <= item->source.begin &&
              list->source.end >= item->source.end,
          "container ranges enclose their children");

    const Event *link = find_event(recorder, 'S', MD_SPAN_A);
    check(link && link->text == "u&v|t!",
          "link attributes are decoded before callbacks");
    const Event *html_link = find_event(recorder, 'S', MD_SPAN_A, 1);
    check(html_link && html_link->text == "q&r|A",
          "structured HTML attributes use the same decoder");
    const Event *code = find_event(recorder, 'B', MD_BLOCK_CODE);
    check(code && code->text == "c&p" &&
              joined_text(recorder).find("&amp; raw") != std::string::npos,
          "fence info is decoded while code content keeps entities raw");

    const Event *math =
        find_event(recorder, 'S', MD_SPAN_LATEXMATH_DISPLAY);
    check(math && math->value == MD_MATH_DOLLAR_DISPLAY &&
              input.substr(math->source.begin,
                           math->source.end - math->source.begin) ==
                  "$$x\r\n+ y$$" &&
              input.substr(math->content.begin,
                           math->content.end - math->content.begin) ==
                  "x\r\n+ y",
          "display math reports exact full and content ranges");

    const Event *details = find_event(recorder, 'B', MD_BLOCK_DETAILS);
    const Event *summary = find_event(recorder, 'B', MD_BLOCK_SUMMARY);
    check(details && details->value == 1 && summary &&
              input.substr(details->source.begin,
                           details->source.end - details->source.begin) ==
                  "<details title='x > y' open><summary>S &amp; T "
                  "<a href='q&amp;r' title='&#65;'>L</a></summary>"
                  "<p>B</p></details>",
          "Tinta HTML is quote-aware and details are structural");

    std::string text = joined_text(recorder);
    check(text.find("S & T") != std::string::npos &&
              text.find("\xF0\x9F\x98\x80") != std::string::npos &&
              text.find("\xE2\x89\x82\xCC\xB8") != std::string::npos &&
              text.find("\xEF\xBF\xBD") != std::string::npos &&
              text.find("&bogus;") != std::string::npos,
          "entities and NULL are decoded with unknown names preserved");

    Recorder aborted;
    aborted.abort_after = 3;
    status = md_parse(input.data(), static_cast<MD_SIZE>(input.size()),
                      &value, &aborted);
    check(status == 7, "callback abort status is propagated");

    MD_PARSER wrong = value;
    wrong.abi_version = 0;
    check(md_parse("x", 1, &wrong, nullptr) == -1,
          "unsupported parser ABI is rejected");

    const std::string missing_summary =
        "<details>Body without summary</details>";
    Recorder synthesized;
    check(md_parse(missing_summary.data(),
                   static_cast<MD_SIZE>(missing_summary.size()), &value,
                   &synthesized) == 0,
          "details without summary parses");
    const Event *synthetic_summary =
        find_event(synthesized, 'B', MD_BLOCK_SUMMARY);
    check(synthetic_summary &&
              synthetic_summary->source.begin == MD_OFFSET_INVALID &&
              joined_text(synthesized).find("Details") != std::string::npos,
          "default summary and text are explicitly synthetic");

    struct ExtensionCase {
        const char *input;
        MD_SPANTYPE type;
        const char *source;
        const char *content;
    };
    static const ExtensionCase extension_cases[] = {
        {"a ==mark== b", MD_SPAN_MARK, "==mark==", "mark"},
        {"a ~~gone~~ b", MD_SPAN_DEL, "~~gone~~", "gone"},
        {"x^2^", MD_SPAN_SUPERSCRIPT, "^2^", "2"},
        {"H~2~O", MD_SPAN_SUBSCRIPT, "~2~", "2"},
        {"== spaced ==", MD_SPAN_MARK, "== spaced ==", " spaced "},
        {"~~ spaced ~~", MD_SPAN_DEL, "~~ spaced ~~", " spaced "},
        {"<p>==html==</p>", MD_SPAN_MARK, "==html==", "html"},
    };
    for (const auto &test : extension_cases) {
        std::string source(test.input);
        Recorder events;
        check(md_parse(source.data(), static_cast<MD_SIZE>(source.size()),
                       &value, &events) == 0 &&
                  events.blocks.empty() && events.spans.empty() &&
                  event_has_source(events, 'S', test.type, source,
                                   test.source) &&
                  text_has_source(events, source, test.content, test.content),
              "extension spans preserve full and content source ranges");
    }

    struct LiteralCase {
        const char *input;
        MD_SPANTYPE type;
    };
    static const LiteralCase literal_cases[] = {
        {"====", MD_SPAN_MARK},
        {"~~~~", MD_SPAN_DEL},
        {"^bad space^", MD_SPAN_SUPERSCRIPT},
        {"~bad\tspace~", MD_SPAN_SUBSCRIPT},
        {"==unclosed", MD_SPAN_MARK},
        {"~~unclosed", MD_SPAN_DEL},
        {"`==code== ~~gone~~ ^x^ ~y~`", MD_SPAN_MARK},
    };
    for (const auto &test : literal_cases) {
        std::string source(test.input);
        Recorder events;
        check(md_parse(source.data(), static_cast<MD_SIZE>(source.size()),
                       &value, &events) == 0 &&
                  count_event(events, 'S', test.type) == 0,
              "invalid or code-isolated extension delimiters remain literal");
    }

    {
        const std::string source = "*==mark==* [~~gone~~](u)";
        Recorder events;
        check(md_parse(source.data(), static_cast<MD_SIZE>(source.size()),
                       &value, &events) == 0 &&
                  count_event(events, 'S', MD_SPAN_EM) == 1 &&
                  count_event(events, 'S', MD_SPAN_MARK) == 1 &&
                  count_event(events, 'S', MD_SPAN_A) == 1 &&
                  count_event(events, 'S', MD_SPAN_DEL) == 1,
              "extensions nest inside existing Markdown spans");
    }

    struct FallbackCase {
        const char *input;
        const char *visible;
        const char *hidden;
    };
    static const FallbackCase fallback_cases[] = {
        {"<unknown>keep</unknown>\n", "keep", nullptr},
        {"<!--gone--><p>keep</p>\n", "keep", "gone"},
        {"</details>\n\nAfter\n", "After", nullptr},
        {"<details><summary>First</summary><summary>Second</summary>Body",
         "FirstSecondBody", nullptr},
        {"<details title='x > y'>Body", "DetailsBody", nullptr},
    };
    for (const auto &test : fallback_cases) {
        std::string source(test.input);
        Recorder events;
        int parse_status = md_parse(
            source.data(), static_cast<MD_SIZE>(source.size()), &value,
            &events);
        std::string visible = joined_text(events);
        check(parse_status == 0 && events.blocks.empty() &&
                  events.spans.empty() &&
                  visible.find(test.visible) != std::string::npos &&
                  (!test.hidden ||
                   visible.find(test.hidden) == std::string::npos),
              "HTML fallback remains visible, balanced and comment-free");
    }

    {
        const std::string source =
            "x <widget title='a > b'>keep</widget> y\n";
        Recorder events;
        check(md_parse(source.data(), static_cast<MD_SIZE>(source.size()),
                       &value, &events) == 0 &&
                  count_event(events, 'T', MD_TEXT_HTML) == 2 &&
                  joined_text(events).find("<widget title='a > b'>") !=
                      std::string::npos,
              "unknown inline HTML remains on the raw HTML callback path");
    }

    {
        std::string source;
        for (int i = 0; i < 257; i++) source += "<blockquote>";
        source += "deep";
        for (int i = 0; i < 257; i++) source += "</blockquote>";
        source += "\n";
        Recorder events;
        check(md_parse(source.data(), static_cast<MD_SIZE>(source.size()),
                       &value, &events) == 0 &&
                  events.blocks.empty() && events.spans.empty() &&
                  joined_text(events).find("deep") != std::string::npos,
              "HTML nesting limit falls back without unbalanced events");
    }

    struct AbortCase {
        char category;
        const char *input;
        int code;
    };
    static const AbortCase abort_cases[] = {
        {'B', "x", 21}, {'b', "x", 22}, {'S', "*x*", 23},
        {'s', "*x*", 24}, {'T', "x", 25},
    };
    for (const auto &test : abort_cases) {
        Recorder events;
        events.abort_category = test.category;
        events.abort_code = test.code;
        check(md_parse(test.input,
                       static_cast<MD_SIZE>(std::strlen(test.input)),
                       &value, &events) == test.code && events.did_abort &&
                  events.calls_after_abort == 0,
              "each callback category aborts immediately with its exact code");
    }

    {
        const std::string oom_input =
            "# OOM\n\n"
            "> *nested* [link](u&amp;v \"title\")\n\n"
            "| a | b |\n| - | - |\n| 1 | 2 |\n\n"
            "```c&amp;p\ncode\n```\n\n"
            "inline <ruby>x<rt>y</rt></ruby>\n\n"
            "<details open><summary>==S==</summary><p>~~B~~</p></details>\n";
        md4c_test_allocator_reset();
        Recorder baseline;
        check(md_parse(oom_input.data(),
                       static_cast<MD_SIZE>(oom_input.size()), &value,
                       &baseline) == 0 &&
                  md4c_test_allocator_live_count() == 0,
              "OOM fixture succeeds without live parser allocations");
        size_t allocation_count = md4c_test_allocator_call_count();
        check(allocation_count > 0,
              "OOM fixture exercises parser allocation paths");
        for (size_t fail_at = 0; fail_at < allocation_count; fail_at++) {
            md4c_test_allocator_reset();
            md4c_test_allocator_fail_at(fail_at);
            Recorder failed_parse;
            int parse_status = md_parse(
                oom_input.data(), static_cast<MD_SIZE>(oom_input.size()),
                &value, &failed_parse);
            check(parse_status == -1 &&
                      md4c_test_allocator_live_count() == 0,
                  "each injected allocation failure returns OOM without leaks");
        }
        md4c_test_allocator_reset();
        Recorder recovered;
        check(md_parse(oom_input.data(),
                       static_cast<MD_SIZE>(oom_input.size()), &value,
                       &recovered) == 0 &&
                  md4c_test_allocator_live_count() == 0,
              "parser recovers after allocator fault injection is reset");
        md4c_test_allocator_reset();
    }
}
