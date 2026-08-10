#pragma once

#include <cstddef>

class TintaTestContext {
public:
    void check(bool condition, const char *message);

private:
    const char *suite_name_ = nullptr;
    const char *case_name_ = nullptr;
    int failures_ = 0;

    friend int tinta_run_tests(const char *suite_name,
                               const struct TintaTestCase *cases,
                               size_t case_count);
};

using TintaTestFunction = void (*)(TintaTestContext &);

struct TintaTestCase {
    const char *name;
    TintaTestFunction function;
};

int tinta_run_tests(const char *suite_name, const TintaTestCase *cases,
                    size_t case_count);
