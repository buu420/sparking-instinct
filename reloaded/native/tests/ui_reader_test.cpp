#include "ui_reader.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

int main()
{
    struct Case {
        std::string_view input;
        std::string_view expected;
    };
    constexpr Case cases[] = {
        {R"(<icon id="Pad_03" table="XSX"/> Confirm)", "A Confirm"},
        {R"(<icon table='PS5' id='Pad_03' /> Confirm)", "Cross Confirm"},
        {R"(<icon id="Pad_03" table="PS4"/> Confirm)", "Cross Confirm"},
        {R"(<icon id="Key_16" table="Windows"/> Confirm)", "Space Confirm"},
        {R"(<icon id="Key_26" table="Windows"/>+<icon id="Pad_Win" table="Windows" text="1"/>)", "Left Shift + 1"},
        {R"(<icon id="Key_26" table="Windows"/>＋<icon id="Key_38" table="Windows"/>)", "Left Shift + Left Mouse Button"},
        {R"(<icon id="Pad_11" table="XSX"/>+<icon id="pad_31" table="XSX"/>)", "RT + D-pad Up or Down"},
        {R"(<icon id="Pad_11" table="PS5"/>+<icon id="Pad_30" table="PS5"/>)", "R2 + D-pad Left or Right"},
        {R"(<icon id="Pad_29" table="XSX"/> Scroll)", "Right Stick Up or Down Scroll"},
        {R"(<icon id="Pad_06" table="XSX"/><icon id="Pad_10" table="XSX"/> Change Screen)", "LB RB Change Screen"},
        {R"(<icon id="Pad_31" table="Windows"/> Action)", "Unidentified control Action"},
        {R"(<icon id="Pad_03" table="Unknown"/> Action)", "Unidentified control Action"},
        {R"(<icon id="Pad_Win" table="Windows" text="&lt;" />)", "<"},
        {"<Highlight>日本語</>\r\n???", "日本語 ???"},
        {"Use 0 &amp; 1", "Use 0 & 1"},
        {"1 < 2", "1 < 2"},
    };
    for (const auto& test : cases) {
        const auto actual = sparking::read_ui_text(test.input);
        if (actual != test.expected) {
            std::cerr << "Expected " << test.expected << ", got " << actual << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
