#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "owo.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include <minIni.h>
#include <utility>

namespace sphaira::ui::menu::homebrew {
namespace {

constexpr const char* SORT_STR[] = {
    "Updated",
    "Size",
    "Alphabetical",
};

constexpr const char* ORDER_STR[] = {
    "Desc",
    "Asc",
};

// returns seconds as: hh:mm:ss
auto TimeFormat(u64 sec) -> std::string {
    char buf[9];

    const auto s = sec % 60;
    const auto h = sec / 60 % 60;
    const auto d = sec / 60 / 60 % 24;

    if (sec < 60) {
        if (!sec) {
            return "00:00:00";
        }
        std::snprintf(buf, sizeof(buf), "00:00:%02lu", s);
    } else if (sec < 3600) {
        std::snprintf(buf, sizeof(buf), "00:%02lu:%02lu", h, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", d, h, s);
    }

    return std::string{buf};
}

} // namespace

Menu::Menu() : MenuBase{"Homebrew"_i18n} {
    this->SetActions(
        std::make_pair(Button::RIGHT, Action{[this](){
            if (m_index < (m_entries.size() - 1) && (m_index + 1) % 3 != 0) {
                SetIndex(m_index + 1);
                App::PlaySoundEffect(SoundEffect_Scroll);
                log_write("moved right\n");
            }
        }}),
        std::make_pair(Button::LEFT, Action{[this](){
            if (m_index != 0 && (m_index % 3) != 0) {
                SetIndex(m_index - 1);
                App::PlaySoundEffect(SoundEffect_Scroll);
                log_write("moved left\n");
            }
        }}),
        std::make_pair(Button::DOWN, Action{[this](){
            if (m_index < (m_entries.size() - 1)) {
                if (m_index < (m_entries.size() - 3)) {
                    SetIndex(m_index + 3);
                } else {
                    SetIndex(m_entries.size() - 1);
                }
                App::PlaySoundEffect(SoundEffect_Scroll);
                if (m_index - m_start >= 9) {
                    log_write("moved down\n");
                    m_start += 3;
                }
            }
        }}),
        std::make_pair(Button::UP, Action{[this](){
            if (m_index >= 3) {
                SetIndex(m_index - 3);
                App::PlaySoundEffect(SoundEffect_Scroll);
                if (m_index < m_start ) {
                    // log_write("moved up\n");
                    m_start -= 3;
                }
            }
        }}),
        std::make_pair(Button::A, Action{"Launch"_i18n, [this](){
            nro_launch(m_entries[m_index].path);
        }}),
        std::make_pair(Button::X, Action{"Options"_i18n, [this](){
            auto options = std::make_shared<Sidebar>("Homebrew Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(options));

            if (m_entries.size()) {
                options->Add(std::make_shared<SidebarEntryCallback>("Sort By"_i18n, [this](){
                    auto options = std::make_shared<Sidebar>("Sort Options"_i18n, Sidebar::Side::RIGHT);
                    ON_SCOPE_EXIT(App::Push(options));

                    SidebarEntryArray::Items sort_items;
                    sort_items.push_back("Updated"_i18n);
                    sort_items.push_back("Size"_i18n);
                    sort_items.push_back("Alphabetical"_i18n);

                    SidebarEntryArray::Items order_items;
                    order_items.push_back("Decending"_i18n);
                    order_items.push_back("Ascending"_i18n);

                    options->Add(std::make_shared<SidebarEntryArray>("Sort"_i18n, sort_items, [this, sort_items](std::size_t& index_out){
                        m_sort.Set(index_out);
                        SortAndFindLastFile();
                    }, m_sort.Get()));

                    options->Add(std::make_shared<SidebarEntryArray>("Order"_i18n, order_items, [this, order_items](std::size_t& index_out){
                        m_order.Set(index_out);
                        SortAndFindLastFile();
                    }, m_order.Get()));
                }));

                #if 0
                options->Add(std::make_shared<SidebarEntryCallback>("Info"_i18n, [this](){

                }));
                #endif

                options->Add(std::make_shared<SidebarEntryCallback>("Delete"_i18n, [this](){
                    const auto buf = "Are you sure you want to delete "_i18n + m_entries[m_index].path.toString() + "?";
                    App::Push(std::make_shared<OptionBox>(
                        buf,
                        "Back"_i18n, "Delete"_i18n, 1, [this](auto op_index){
                            if (op_index && *op_index) {
                                if (R_SUCCEEDED(fs::FsNativeSd().DeleteFile(m_entries[m_index].path))) {
                                    m_entries.erase(m_entries.begin() + m_index);
                                    SetIndex(m_index ? m_index - 1 : 0);
                                }
                            }
                        }
                    ));
                }, true));

                options->Add(std::make_shared<SidebarEntryBool>("Hide Sphaira"_i18n, m_hide_sphaira.Get(), [this](bool& enable){
                    m_hide_sphaira.Set(enable);
                }, "Enabled"_i18n, "Disabled"_i18n));

                options->Add(std::make_shared<SidebarEntryCallback>("Install Forwarder"_i18n, [this](){
                    App::Push(std::make_shared<OptionBox>(
                        "WARNING: Installing forwarders will lead to a ban!"_i18n,
                        "Back"_i18n, "Install"_i18n, 0, [this](auto op_index){
                            if (op_index && *op_index) {
                                InstallHomebrew();
                            }
                        }
                    ));
                }, true));
            }
        }})
    );
}

Menu::~Menu() {
    auto vg = App::GetVg();

    for (auto&p : m_entries) {
        nvgDeleteImage(vg, p.image);
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const u64 SCROLL = m_start;
    const u64 max_entry_display = 9;
    const u64 nro_total = m_entries.size();
    const u64 cursor_pos = m_index;

    // only draw scrollbar if needed
    if (nro_total > max_entry_display) {
        const auto scrollbar_size = 500.f;
        const auto sb_h = 3.f / (float)nro_total * scrollbar_size;
        const auto sb_y = SCROLL / 3.f;
        gfx::drawRect(vg, SCREEN_WIDTH - 50, 100, 10, scrollbar_size, theme->elements[ThemeEntryID_GRID].colour);
        gfx::drawRect(vg, SCREEN_WIDTH - 50+2, 102 + sb_h * sb_y, 10-4, sb_h + (sb_h * 2) - 4, theme->elements[ThemeEntryID_TEXT_SELECTED].colour);
    }

    for (u64 i = 0, pos = SCROLL, y = 110, w = 370, h = 155; pos < nro_total && i < max_entry_display; y += h + 10) {
        for (u64 j = 0, x = 75; j < 3 && pos < nro_total && i < max_entry_display; j++, i++, pos++, x += w + 10) {
            auto& e = m_entries[pos];

            // lazy load image
            if (!e.image && e.icon.empty() && e.icon_size && e.icon_offset) {
                e.icon = nro_get_icon(e.path, e.icon_size, e.icon_offset);
                if (!e.icon.empty()) {
                    e.image = nvgCreateImageMem(vg, 0, e.icon.data(), e.icon.size());
                }
            }

            auto text_id = ThemeEntryID_TEXT;
            if (pos == cursor_pos) {
                text_id = ThemeEntryID_TEXT_SELECTED;
                gfx::drawRectOutline(vg, 4.f, theme->elements[ThemeEntryID_SELECTED_OVERLAY].colour, x, y, w, h, theme->elements[ThemeEntryID_SELECTED].colour);
            } else {
                DrawElement(x, y, w, h, ThemeEntryID_GRID);
            }

            const float image_size = 115;
            gfx::drawImageRounded(vg, x + 20, y + 20, image_size, image_size, e.image);

            nvgSave(vg);
            nvgScissor(vg, x, y, w - 30.f, h); // clip
            {
                const float font_size = 18;
                const float diff = 32;
                #if 1
                gfx::drawTextArgs(vg, x + 148, y + 45, font_size, NVG_ALIGN_LEFT, theme->elements[text_id].colour, "%s", e.GetName());
                gfx::drawTextArgs(vg, x + 148, y + 80, font_size, NVG_ALIGN_LEFT, theme->elements[text_id].colour, e.GetAuthor());
                gfx::drawTextArgs(vg, x + 148, y + 115, font_size, NVG_ALIGN_LEFT, theme->elements[text_id].colour, e.GetDisplayVersion());
                #else
                gfx::drawTextArgs(vg, x + 148, y + 35, font_size, NVG_ALIGN_LEFT, theme->elements[text_id].colour, "%s", e.GetName());
                gfx::drawTextArgs(vg, x + 148, y + 35 + (diff * 1), font_size, NVG_ALIGN_LEFT, theme->elements[text_id].colour, e.GetAuthor());
                gfx::drawTextArgs(vg, x + 148, y + 35 + (diff * 2), font_size, NVG_ALIGN_LEFT, theme->elements[text_id].colour, e.GetDisplayVersion());
                // gfx::drawTextArgs(vg, x + 148, y + 110, font_size, NVG_ALIGN_LEFT, theme->elements[text_id].colour, "PlayCount: %u", e.hbini.launch_count);
                #endif
            }
            nvgRestore(vg);
        }
    }
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_entries.empty()) {
        ScanHomebrew();
    }
}

void Menu::SetIndex(std::size_t index) {
    m_index = index;
    if (!m_index) {
        m_start = 0;
    }

    const auto& e = m_entries[m_index];
    // TimeCalendarTime caltime;
    // timeToCalendarTimeWithMyRule()
    // todo: fix GetFileTimeStampRaw being different to timeGetCurrentTime
    // log_write("name: %s hbini.ts: %lu file.ts: %lu smaller: %s\n", e.GetName(), e.hbini.timestamp, e.timestamp.modified, e.hbini.timestamp < e.timestamp.modified ? "true" : "false");

    SetTitleSubHeading(m_entries[m_index].path);
    this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_entries.size()));
}

void Menu::InstallHomebrew() {
    const auto& nro = m_entries[m_index];
    InstallHomebrew(nro.path, nro.nacp, nro.icon);
}

void Menu::ScanHomebrew() {
    TimeStamp ts;
    nro_scan("/switch", m_entries, m_hide_sphaira.Get());
    log_write("nros found: %zu time_taken: %.2f\n", m_entries.size(), ts.GetSeconds());

    struct IniUser {
        std::vector<NroEntry>& entires;
        Hbini* ini;
        std::string last_section;
    } ini_user{ m_entries };

    ini_browse([](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto user = static_cast<IniUser*>(UserData);

        if (user->last_section != Section) {
            user->last_section = Section;
            user->ini = nullptr;

            for (auto& e : user->entires) {
                if (e.path == Section) {
                    user->ini = &e.hbini;
                    break;
                }
            }
        }

        if (user->ini) {
            if (!strcmp(Key, "timestamp")) {
                user->ini->timestamp = atoi(Value);
            } else if (!strcmp(Key, "launch_count")) {
                user->ini->launch_count = atoi(Value);
            }
        }

        // log_write("found: %s %s %s\n", Section, Key, Value);
        return 1;
    }, &ini_user, App::PLAYLOG_PATH);

    this->Sort();
    SetIndex(0);
}

void Menu::Sort() {
    // returns true if lhs should be before rhs
    const auto sort = m_sort.Get();
    const auto order = m_order.Get();

    const auto sorter = [this, sort, order](const NroEntry& lhs, const NroEntry& rhs) -> bool {
        switch (sort) {
            case SortType_Updated: {
                auto lhs_timestamp = lhs.hbini.timestamp;
                auto rhs_timestamp = rhs.hbini.timestamp;
                if (lhs.timestamp.is_valid && lhs_timestamp < lhs.timestamp.modified) {
                    lhs_timestamp = lhs.timestamp.modified;
                }
                if (rhs.timestamp.is_valid && rhs_timestamp < rhs.timestamp.modified) {
                    rhs_timestamp = rhs.timestamp.modified;
                }

                if (lhs_timestamp == rhs_timestamp) {
                    return strcasecmp(lhs.GetName(), rhs.GetName()) < 0;
                } else if (order == OrderType_Decending) {
                    return lhs_timestamp > rhs_timestamp;
                } else {
                    return lhs_timestamp < rhs_timestamp;
                }
            } break;
            case SortType_Size: {
                if (lhs.size == rhs.size) {
                    return strcasecmp(lhs.GetName(), rhs.GetName()) < 0;
                } else if (order == OrderType_Decending) {
                    return lhs.size > rhs.size;
                } else {
                    return lhs.size < rhs.size;
                }
            } break;
            case SortType_Alphabetical: {
                if (order == OrderType_Decending) {
                    return strcasecmp(lhs.GetName(), rhs.GetName()) < 0;
                } else {
                    return strcasecmp(lhs.GetName(), rhs.GetName()) > 0;
                }
            } break;
        }

        std::unreachable();
    };

    std::sort(m_entries.begin(), m_entries.end(), sorter);
}

void Menu::SortAndFindLastFile() {
    Sort();
}

Result Menu::InstallHomebrew(const fs::FsPath& path, const NacpStruct& nacp, const std::vector<u8>& icon) {
    OwoConfig config{};
    config.nro_path = path.toString();
    config.nacp = nacp;
    config.icon = icon;
    return App::Install(config);
}

Result Menu::InstallHomebrewFromPath(const fs::FsPath& path) {
    NacpStruct nacp;
    R_TRY(nro_get_nacp(path, nacp))
    const auto icon = nro_get_icon(path);
    return InstallHomebrew(path, nacp, icon);
}

} // namespace sphaira::ui::menu::homebrew
