#include "test_harness.h"

#include <iostream>

void TintaTestContext::check(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL [" << suite_name_ << '/' << case_name_ << "]: "
              << message << '\n';
    failures_++;
}

int tinta_run_tests(const char *suite_name, const TintaTestCase *cases,
                    size_t case_count) {
    TintaTestContext context;
    context.suite_name_ = suite_name;

    for (size_t index = 0; index < case_count; index++) {
        context.case_name_ = cases[index].name;
        const int failures_before = context.failures_;
        cases[index].function(context);
        if (context.failures_ == failures_before)
            std::cout << "PASS [" << suite_name << '/' << cases[index].name
                      << "]\n";
    }

    if (!context.failures_) {
        std::cout << suite_name << " tests passed\n";
        return 0;
    }
    std::cerr << context.failures_ << " assertion(s) failed in " << suite_name
              << " tests\n";
    return 1;
}
