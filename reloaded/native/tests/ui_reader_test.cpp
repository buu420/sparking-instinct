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
        {R"(<icon id="Pad_Win" table="Windows" text="Q"/>+<icon id="Key_19" table="Windows"/> Reset Positions)", "Q + Tab Reset Positions"},
        {R"(<icon id="Pad_06" table="XSX"/>+<icon id="Pad_00" table="XSX"/> Reset Positions)", "LB + Menu Reset Positions"},
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
    struct Selection {
        std::string_view root, hit, visual;
        bool expected;
    };
    constexpr Selection selections[] = {
        {"WBP_GRP_BS_StageList_DP2_C", "WBP_OBJ_Common_HitBtn_Text_12", "WBP_OBJ_BS_BTN_Menu_DP_12", true},
        {"WBP_GRP_BS_StageList_DP2_C", "WBP_OBJ_Common_HitBtn_Text_12", "WBP_OBJ_BS_BTN_Menu_DP_11", false},
        {"WBP_GRP_BS_SetBGM_List_C", "WBP_OBJ_Common_HitButton_01", "WBP_OBJ_BS_SetBGMBTN_01", true},
        {"WBP_GRP_BS_RuleSetting_DP_C", "WBP_OBJ_Common_HitBtn_Text_01", "WBP_OBJ_BS_SetRuleBTN_01", true},
        {"WBP_GRP_BS_StageList_DP2_C", "WBP_OBJ_Common_HitBtn_Text_01", "WBP_OBJ_BS_SetRuleBTN_01", false},
        {"WBP_GRP_BS_CharaList_DP_C", "WBP_OBJ_Common_HitButton_06", "WBP_OBJ_BS_CharaIcon_06", true},
        {"WBP_GRP_BS_Top_00_1P_C", "WBP_OBJ_Common_HitBTN_Remove", "WBP_OBJ_Com_BTN_Remove", true},
        {"WBP_GRP_BS_Top_00_2P_C", "WBP_OBJ_Common_HitBTN_Replace", "WBP_OBJ_Com_BTN_Replace", true},
        {"WBP_GRP_BS_CharaList_DP_C", "WBP_OBJ_Common_HitBTN_Remove", "WBP_OBJ_Com_BTN_Remove", false},
        {"WBP_GRP_CSG_Top_00_C", "WBP_OBJ_Common_HitButton_1", "WBP_OBJ_CSG_BTN_00", true},
        {"WBP_GRP_CSG_Top_00_C", "WBP_OBJ_Common_HitButton_1", "WBP_OBJ_CSG_BTN_01", false},
        {"WBP_GRP_AI_CharacterSelect_C", "WBP_OBJ_Common_HitButton", "WBP_OBJ_AI_BTN_Menu_00", true},
        {"WBP_GRP_AI_CharacterSelect_3piece_C", "WBP_OBJ_Common_HitButton_5", "WBP_OBJ_AI_BTN_Menu_05", true},
        {"WBP_GRP_AI_CharacterSelect_C", "WBP_OBJ_Common_HitButton_5", "WBP_OBJ_AI_BTN_Menu_06", false},
        {"WBP_GRP_AI_Map_00_C", "WBP_OBJ_Common_HitButton_5", "WBP_OBJ_AI_BTN_Menu_05", false},
    };
    for (const auto& test : selections) {
        if (sparking::ui_detail::matches_menu_visual(test.root, test.hit, test.visual) != test.expected) {
            std::cerr << "Unexpected hit target pairing for " << test.root << ", " << test.hit
                      << ", " << test.visual << '\n';
            return EXIT_FAILURE;
        }
    }
    using sparking::ui_detail::compose_help_prompt;
    if (compose_help_prompt("LB + Menu Reset Positions", "B", "Back") != "LB + Menu Reset Positions" ||
        compose_help_prompt("", "Backspace", "Back") != "Backspace, Back" ||
        !compose_help_prompt("", "", "Back").empty() ||
        !compose_help_prompt("", "Backspace", "").empty()) {
        std::cerr << "Guide prompt must use the complete visible binding and action\n";
        return EXIT_FAILURE;
    }
    struct ResetPrompt {
        std::string_view painted;
        bool expected;
    };
    constexpr ResetPrompt reset_prompts[] = {
        {R"(<icon id="Pad_06" table="XSX"/>+<icon id="Pad_00" table="XSX"/> Reset Positions)", true},
        {R"(<icon id="Pad_Win" table="Windows" text="Q"/>+<icon id="Key_19" table="Windows"/> Reset Positions)", true},
        {R"(<icon id="Pad_06" table="PS5"/>＋<icon id="Pad_00" table="PS5"/> 位置をリセット)", true},
        {R"(<icon id="Pad_06" table="XSX"/><icon id="Pad_10" table="XSX"/> Change Screen)", false},
        {R"(<icon id="Pad_00" table="XSX"/> Options)", false},
        {R"(<icon id="Key_19" table="Windows"/> Options)", false},
        {R"(<icon id="Pad_06" table="XSX"/>+<icon id="Pad_00" table="PS5"/> Reset Positions)", false},
        {R"(<icon id="Pad_06" table="XSX"/>+<icon id="Pad_00" table="XSX"/> )", false},
        {"Reset Positions", false},
    };
    for (const auto& test : reset_prompts) {
        if (sparking::ui_detail::matches_training_reset_prompt(test.painted) != test.expected) {
            std::cerr << "Unexpected battle guide classification for " << test.painted << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
