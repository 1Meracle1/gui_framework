#pragma once

#include <base/string_buffer.h>
#include <cstddef>
#include <cstdint>
#include <gui/gui.h>

namespace ui_api_testbed {

    enum class LiquidGlassTheme : uint8_t {
        DARK,
        LIGHT,
    };

    inline constexpr size_t PREVIEW_TABLE_COLUMN_COUNT = 3u;
    inline constexpr size_t PREVIEW_TABLE_ROW_COUNT = 4u;
    inline constexpr size_t SAMPLE_TABLE_COLUMN_COUNT = 3u;
    inline constexpr size_t SAMPLE_TABLE_ROW_COUNT = 4u;

    struct TestbedState {
        bool enabled = true;
        bool preview = true;
        bool read_only_value = false;
        bool reveal_asset_list = true;
        bool reveal_log_scroll = true;
        bool popup_open = false;
        bool modal_open = false;
        bool sample_enabled = true;
        bool sample_preview = false;
        LiquidGlassTheme theme = LiquidGlassTheme::DARK;
        float scale = 1.25f;
        float image_preview_zoom = 1.0f;
        float sample_value = 0.5f;
        float sample_loading_phase = 0.0f;
        size_t selected_tab = 0u;
        size_t selected_index = 12u;
        size_t size_mode = 1u;
        size_t image_preview_sample = 0u;
        size_t preview_table_sort_count = 0u;
        size_t sample_table_sort_count = 0u;
        char name[64] = "Editable text";
        char sample_name[64] = "Second tab text";
        gui::TableSortColumn preview_table_sort_columns[PREVIEW_TABLE_COLUMN_COUNT] = {};
        gui::TableSortColumn sample_table_sort_columns[SAMPLE_TABLE_COLUMN_COUNT] = {};
        gui::TableFilterColumn sample_table_filter_columns[SAMPLE_TABLE_COLUMN_COUNT] = {};
        gui::TableFilterValue sample_table_item_filter_values[SAMPLE_TABLE_ROW_COUNT] = {
            {"Checkbox"}, {"Popup"}, {"Slider"}, {"Table"}
        };
        gui::TableFilterValue sample_table_layer_filter_values[3] = {
            {"Input"}, {"Overlay"}, {"Layout"}
        };
        gui::TableFilterValue sample_table_state_filter_values[SAMPLE_TABLE_ROW_COUNT] = {
            {"Enabled"}, {"Closed"}, {"Ready"}, {"Sortable"}
        };
        bool preview_table_selected_columns[PREVIEW_TABLE_COLUMN_COUNT] = {true, true, false};
        bool sample_table_selected_columns[SAMPLE_TABLE_COLUMN_COUNT] = {true, true, false};
        bool sample_table_filter_open[SAMPLE_TABLE_COLUMN_COUNT] = {};
        char sample_table_item_filter[32] = {};
        char sample_table_layer_filter[32] = {};
        char sample_table_state_filter[32] = {};
        gui::TextSelection title_selection = {};
        gui::TextSelection body_selection = {};
        gui::Signal header_signal = {};
        gui::Signal selected_row_signal = {};
        StringBuffer multiline_text_buffer;
    };

    struct TextureSample {
        gui::render::Texture texture = {};
        gui::Vec2 size = {};
    };

    struct TestbedTextures {
        TextureSample disk = {};
        TextureSample embedded = {};
    };

    inline constexpr char BODY_TEXT[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.m dolor sit amet, "
        "consecteturdolor sit amet, consectetur "
        "adipiscing elit.m dolor sit amet, consectetur adipiscing elit.\n"
        "Integer posuere erat a ante venenatis dapibus posuere velit aliquet.\n\n"
        "Donec ullamcorper nulla non metus auctor fringilla.\n"
        "Cras mattis consectetur purus sit amet fermentum.\n\n"
        "Maecenas sed diam eget risus varius blandit sit amet non magna.\n"
        "Vestibulum id ligula porta felis euismod semper.";
    inline constexpr char CLOSE_GLYPH[] = "\xC3\x97";

    struct PreviewTableRow {
        StrRef group = {};
        StrRef task = {};
        StrRef status = {};
    };

    inline constexpr PreviewTableRow PREVIEW_TABLE_ROWS[PREVIEW_TABLE_ROW_COUNT] = {
        {"Layout", "Build table element", "Active"},
        {"Input", "Edit multiline text", "Ready"},
        {"Render", "Clip table cells", "Done"},
        {"Overlay", "Stack floating layers", "Open"},
    };

    struct SampleTableRow {
        StrRef item = {};
        StrRef layer = {};
        StrRef state = {};
    };

    inline constexpr SampleTableRow SAMPLE_TABLE_ROWS[SAMPLE_TABLE_ROW_COUNT] = {
        {"Checkbox", "Input", "Enabled"},
        {"Popup", "Overlay", "Closed"},
        {"Slider", "Input", "Ready"},
        {"Table", "Layout", "Sortable"},
    };

} // namespace ui_api_testbed
