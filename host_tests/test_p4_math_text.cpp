#include "p4_math_text.hpp"
#include <cassert>
#include <cstdio>

int main() {
    const auto all=[](uint32_t){return true;};
    // Actual p0-targets164 reader result: correct equation, raw controls on LCD.
    const std::string original=R"(讲解方程(3)：1.6 \\times 2 - 3x = 40\\%)";
    assert(p4_math::display(original.c_str(),all)=="讲解方程(3)：1.6 × 2 - 3x = 40%");
    assert(original==R"(讲解方程(3)：1.6 \\times 2 - 3x = 40\\%)");
    assert(p4_math::display(R"(1.6 \times 2-3x=40\%)",all)=="1.6 × 2-3x=40%");
    assert(p4_math::display(R"(\frac{1}{2}-2x=\frac{1}{3})",all)=="(1)/(2)-2x=(1)/(3)");
    assert(p4_math::display(R"(\\frac{x+1}{2x})",all)=="(x+1)/(2x)");
    assert(p4_math::display(R"(\frac{1+\frac{2}{3}}{4})",all)=="(1+(2)/(3))/(4)");
    for(const char *unknown:{R"(\timescale)",R"(\fraction{1}{2})",R"(\frac{x}{)",R"(\frac{x})",R"(a\\b)",R"(\\\times)"})
        assert(p4_math::display(unknown,all)==unknown);
    assert(p4_math::display("x²−3=0",[](uint32_t c){return c!=0xb2;})=="x^(2)−3=0");
    assert(p4_math::display("x²−3=0",all)=="x²−3=0");
    std::puts("Retained equation display, grouping, unknown controls and glyph fallback passed");
}
