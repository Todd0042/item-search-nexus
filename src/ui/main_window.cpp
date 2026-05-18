#include "main_window.h"
#include "data/item_db.h"
#include "data/player_items.h"
#include "icon_cache.h"
#include "api/gw2_api.h"
#include "addon.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <thread>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

static std::string FormatCount(int n)
{
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3)
        s.insert(i, ",");
    return s;
}

MainWindow& MainWindow::Instance()
{
    static MainWindow instance;
    return instance;
}

void MainWindow::Initialize()
{
    // Show setup wizard if no API key is configured
    if (Gw2Api::Instance().GetApiKey().empty())
    {
        m_showSetupWizard = true;
        m_visible = true;
    }
}

static const char* RarityToString(ItemRarity r)
{
    switch (r)
    {
        case ItemRarity::Junk: return "Junk";
        case ItemRarity::Basic: return "Basic";
        case ItemRarity::Fine: return "Fine";
        case ItemRarity::Masterwork: return "Masterwork";
        case ItemRarity::Rare: return "Rare";
        case ItemRarity::Exotic: return "Exotic";
        case ItemRarity::Ascended: return "Ascended";
        case ItemRarity::Legendary: return "Legendary";
        default: return "Unknown";
    }
}

static ImU32 RarityToColor(ItemRarity r)
{
    switch (r)
    {
        case ItemRarity::Junk: return IM_COL32(211, 211, 211, 255);
        case ItemRarity::Basic: return IM_COL32(255, 255, 255, 255);
        case ItemRarity::Fine: return IM_COL32(0, 191, 255, 255);
        case ItemRarity::Masterwork: return IM_COL32(34, 139, 34, 255);
        case ItemRarity::Rare: return IM_COL32(255, 255, 0, 255);
        case ItemRarity::Exotic: return IM_COL32(255, 165, 0, 255);
        case ItemRarity::Ascended: return IM_COL32(255, 105, 180, 255);
        case ItemRarity::Legendary: return IM_COL32(138, 43, 226, 255);
        default: return IM_COL32(255, 255, 255, 255);
    }
}

static const char* ItemSourceToString(InventoryItemSource source)
{
    switch (source)
    {
        case InventoryItemSource::Bank: return "Bank";
        case InventoryItemSource::SharedInventory: return "Shared Inventory";
        case InventoryItemSource::MaterialStorage: return "Materials";
        case InventoryItemSource::TradingPostDeliveryBox: return "TP Delivery Box";
        case InventoryItemSource::TradingPostSellOrder: return "TP Sell Orders";
        case InventoryItemSource::CharacterInventory: return "Character Inventory";
        case InventoryItemSource::CharacterEquipment: return "Character Equipment";
        default: return "Other";
    }
}

static const char* ItemTypeToString(ItemType t)
{
    switch (t)
    {
        case ItemType::Armor: return "Armor";
        case ItemType::Back: return "Back";
        case ItemType::Bag: return "Bag";
        case ItemType::Consumable: return "Consumable";
        case ItemType::Container: return "Container";
        case ItemType::CraftingMaterial: return "Crafting Material";
        case ItemType::Gathering: return "Gathering";
        case ItemType::Gizmo: return "Gizmo";
        case ItemType::Key: return "Key";
        case ItemType::MiniPet: return "Mini Pet";
        case ItemType::Tool: return "Tool";
        case ItemType::Trait: return "Trait";
        case ItemType::Trinket: return "Trinket";
        case ItemType::Trophy: return "Trophy";
        case ItemType::UpgradeComponent: return "Upgrade Component";
        case ItemType::Weapon: return "Weapon";
        case ItemType::PowerCore: return "Power Core";
        case ItemType::JadeTechModule: return "Jade Tech Module";
        case ItemType::Relic: return "Relic";
        default: return "All Types";
    }
}

static const char* ItemSubTypeToString(ItemSubType st)
{
    switch (st)
    {
        case ItemSubType::Armor_Boots: return "Boots";
        case ItemSubType::Armor_Coat: return "Coat";
        case ItemSubType::Armor_Gloves: return "Gloves";
        case ItemSubType::Armor_Helm: return "Helm";
        case ItemSubType::Armor_HelmAquatic: return "Aquatic Helm";
        case ItemSubType::Armor_Leggings: return "Leggings";
        case ItemSubType::Armor_Shoulders: return "Shoulders";
        case ItemSubType::Weapon_Axe: return "Axe";
        case ItemSubType::Weapon_Dagger: return "Dagger";
        case ItemSubType::Weapon_Mace: return "Mace";
        case ItemSubType::Weapon_Pistol: return "Pistol";
        case ItemSubType::Weapon_Scepter: return "Scepter";
        case ItemSubType::Weapon_Sword: return "Sword";
        case ItemSubType::Weapon_Focus: return "Focus";
        case ItemSubType::Weapon_Shield: return "Shield";
        case ItemSubType::Weapon_Torch: return "Torch";
        case ItemSubType::Weapon_Warhorn: return "Warhorn";
        case ItemSubType::Weapon_Greatsword: return "Greatsword";
        case ItemSubType::Weapon_Hammer: return "Hammer";
        case ItemSubType::Weapon_LongBow: return "Long Bow";
        case ItemSubType::Weapon_Rifle: return "Rifle";
        case ItemSubType::Weapon_ShortBow: return "Short Bow";
        case ItemSubType::Weapon_Staff: return "Staff";
        case ItemSubType::Weapon_Harpoon: return "Harpoon";
        case ItemSubType::Weapon_Speargun: return "Speargun";
        case ItemSubType::Weapon_Trident: return "Trident";
        case ItemSubType::Weapon_LargeBundle: return "Bundle";
        case ItemSubType::Weapon_SmallBundle: return "Small Bundle";
        case ItemSubType::Weapon_Toy: return "Toy Weapon";
        case ItemSubType::Weapon_ToyTwoHanded: return "Two-Handed Toy";
        case ItemSubType::Trinket_Accessory: return "Accessory";
        case ItemSubType::Trinket_Amulet: return "Amulet";
        case ItemSubType::Trinket_Ring: return "Ring";
        case ItemSubType::UpgradeComponent_Default: return "Upgrade";
        case ItemSubType::UpgradeComponent_Gem: return "Gem";
        case ItemSubType::UpgradeComponent_Rune: return "Rune";
        case ItemSubType::UpgradeComponent_Sigil: return "Sigil";
        default: return "All Subtypes";
    }
}

void MainWindow::Render()
{
    if (m_firstFrame)
    {
        ImGui::SetNextWindowSize(ImVec2(680, 600), ImGuiCond_FirstUseEver);
        m_firstFrame = false;
    }

    // Show setup wizard on first launch
    if (m_showSetupWizard)
    {
        RenderSetupWizard();
        if (m_showSetupWizard)
            return;
    }

    if (!m_visible)
        return;

    ImGui::Begin("Item Search", &m_visible, ImGuiWindowFlags_NoCollapse);

    auto& playerItems = PlayerItems::Instance();
    bool ready = playerItems.IsReady();
    bool offline = playerItems.IsOfflineReady();

    if (!ready && !offline)
    {
        int itemCount = ItemDb::Instance().Count();
        FetchMode fetchMode = ItemDb::Instance().GetFetchMode();
        float progress = playerItems.Progress();
        int totalItems = playerItems.TotalItemCount();

        ImGui::TextWrapped("Loading data from Guild Wars 2 API...");

        char buf[512];

        if (fetchMode == FetchMode::OnDemand)
        {
            snprintf(buf, sizeof(buf), "No item database \xe2\x80\x94 fetching item details on demand");
        }
        else
        {
            snprintf(buf, sizeof(buf), "Items in database: %s", FormatCount(itemCount).c_str());
        }
        ImGui::TextUnformatted(buf);

        if (totalItems > 0)
        {
            snprintf(buf, sizeof(buf), "Items found in your account: %s", FormatCount(totalItems).c_str());
            ImGui::TextUnformatted(buf);
        }

        if (progress > 0.0f)
        {
            ImGui::ProgressBar(progress, ImVec2(-1, 0));
        }

        if (!Gw2Api::Instance().GetApiKey().empty())
        {
            if (fetchMode == FetchMode::OnDemand)
            {
                ImGui::TextWrapped("Scanning your account for items... (no database being created, item details will be fetched as needed)");
            }
            else if (fetchMode == FetchMode::CreateDBParallel)
            {
                ImGui::TextWrapped("Creating item database in parallel (~60 seconds). You may notice some in-game lag during this process.");
                ImGui::NewLine();
                ImGui::TextWrapped("This first scan fetches every item from every character's inventory, bank, material storage, and trading post. It can take several minutes.");
            }
            else
            {
                ImGui::TextWrapped("This first scan fetches every item from every character's inventory, bank, material storage, and trading post. It can take several minutes (up to 10 for larger accounts).");
                ImGui::NewLine();
                ImGui::TextWrapped("Subsequent loads are much faster \xe2\x80\x94 bank, material storage, shared inventory, and trading post are always refreshed, but individual characters are only re-scanned if they've gained playtime since the last scan. Unchanged characters load from disk cache.");
            }
        }
        else
        {
            ImGui::TextUnformatted("No API key set. Open Settings (F12) and enter your GW2 API key.");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Gw2ItemSearch \xc2\xa9 tentose (MIT) \xe2\x80\x94 ported to Nexus by Todd0042");

        ImGui::End();
        return;
    }

    // Offline mode overlay — show progress bar at top while keeping search usable
    if (!ready && offline)
    {
        float progress = playerItems.Progress();
        if (progress > 0.0f)
        {
            ImGui::ProgressBar(progress, ImVec2(-1, 0));
        }
        else
        {
            ImGui::TextUnformatted("Starting refresh...");
        }
        int totalItems = playerItems.TotalItemCount();
        if (totalItems > 0)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "Items found: %s", FormatCount(totalItems).c_str());
            ImGui::TextUnformatted(buf);
        }
        ImGui::TextUnformatted("Fetching latest data from API \xe2\x80\x94 showing offline view");
        ImGui::Separator();
    }

    m_wasReady = ready;

    RenderSearchBar();
    ImGui::Separator();
    RenderResults();

    ImGui::End();

    if (m_showSettings)
    {
        RenderSettingsWindow();
    }
}

void MainWindow::RenderSearchBar()
{
    static char searchBuf[256] = { 0 };
    strncpy(searchBuf, m_searchQuery.c_str(), sizeof(searchBuf) - 1);

    float searchBtnWidth = ImGui::CalcTextSize("Search").x + ImGui::GetStyle().FramePadding.x * 2;
    float filtersBtnWidth = ImGui::CalcTextSize("Filters").x + ImGui::GetStyle().FramePadding.x * 2;
    float settingsBtnWidth = ImGui::CalcTextSize("Settings").x + ImGui::GetStyle().FramePadding.x * 2;
    float totalBtnWidth = searchBtnWidth + filtersBtnWidth + settingsBtnWidth + ImGui::GetStyle().ItemSpacing.x * 4;

    ImGui::PushItemWidth(-totalBtnWidth);
    bool enterPressed = ImGui::InputTextWithHint("##search", "Search items or use Filters to browse", searchBuf, sizeof(searchBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActivated())
        m_showFilters = false;
    ImGui::PopItemWidth();

    m_searchQuery = searchBuf;

    bool canSearch = m_searchQuery.size() >= 3 || m_options.HasActiveFilter();

    ImGui::SameLine();
    bool searchClicked = false;
    if (!canSearch)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::Button("Search");
        ImGui::PopStyleVar();
    }
    else
    {
        searchClicked = ImGui::Button("Search") || enterPressed;
    }

    ImGui::SameLine();
    if (ImGui::Button("Filters"))
    {
        m_showFilters = !m_showFilters;
    }

    ImGui::SameLine();
    if (ImGui::Button("Settings"))
    {
        m_showSettings = !m_showSettings;
    }

    if (m_showFilters)
    {
        RenderFilterPanel();
    }

    if (searchClicked)
    {
        if (m_searchQuery.size() >= 3)
            PerformSearch(m_searchQuery);
        else if (m_options.HasActiveFilter())
            PerformBrowse();
    }
}

void MainWindow::RenderFilterPanel()
{
    ImGui::BeginChild("##filter_panel", ImVec2(0, 120), true);

    ImGui::Text("Filters");
    ImGui::Separator();

    // Type dropdown
    const char* typeItems = "All Types\0Armor\0Back\0Bag\0Consumable\0Container\0Crafting Material\0Gathering\0Gizmo\0Key\0Mini Pet\0Tool\0Trait\0Trinket\0Trophy\0Upgrade Component\0Weapon\0Power Core\0Jade Tech Module\0Relic\0";
    if (ImGui::Combo("Type", &m_selectedTypeIdx, typeItems))
    {
        static ItemType typeMap[] = {
            ItemType::Unknown, ItemType::Armor, ItemType::Back, ItemType::Bag,
            ItemType::Consumable, ItemType::Container, ItemType::CraftingMaterial,
            ItemType::Gathering, ItemType::Gizmo, ItemType::Key, ItemType::MiniPet,
            ItemType::Tool, ItemType::Trait, ItemType::Trinket, ItemType::Trophy,
            ItemType::UpgradeComponent, ItemType::Weapon, ItemType::PowerCore,
            ItemType::JadeTechModule, ItemType::Relic
        };
        m_options.Type = typeMap[m_selectedTypeIdx];
        m_selectedSubTypeIdx = 0;
        m_options.SubType = ItemSubType::Unknown;
        m_subTypes = ItemDb::GetSubTypesForType(m_options.Type);
    }

    // Sub-type dropdown
    if (!m_subTypes.empty())
    {
        std::string preview = m_selectedSubTypeIdx == 0
            ? "All " + std::string(ItemDb::ItemTypeToDisplayName(m_options.Type))
            : ItemDb::ItemSubTypeToDisplayName(m_subTypes[m_selectedSubTypeIdx - 1]);

        if (ImGui::BeginCombo("Sub-type", preview.c_str()))
        {
            bool isSelected = (m_selectedSubTypeIdx == 0);
            if (ImGui::Selectable(("All " + std::string(ItemDb::ItemTypeToDisplayName(m_options.Type))).c_str(), isSelected))
            {
                m_selectedSubTypeIdx = 0;
                m_options.SubType = ItemSubType::Unknown;
            }
            for (int i = 0; i < (int)m_subTypes.size(); i++)
            {
                isSelected = (m_selectedSubTypeIdx == i + 1);
                if (ImGui::Selectable(ItemDb::ItemSubTypeToDisplayName(m_subTypes[i]), isSelected))
                {
                    m_selectedSubTypeIdx = i + 1;
                    m_options.SubType = m_subTypes[i];
                }
            }
            ImGui::EndCombo();
        }
    }

    // Rarity dropdown
    const char* rarityItems = "All Rarities\0Junk\0Basic\0Fine\0Masterwork\0Rare\0Exotic\0Ascended\0Legendary\0";
    if (ImGui::Combo("Rarity", &m_selectedRarityIdx, rarityItems))
    {
        static ItemRarity rarityMap[] = {
            ItemRarity::Unknown, ItemRarity::Junk, ItemRarity::Basic, ItemRarity::Fine,
            ItemRarity::Masterwork, ItemRarity::Rare, ItemRarity::Exotic,
            ItemRarity::Ascended, ItemRarity::Legendary
        };
        m_options.Rarity = rarityMap[m_selectedRarityIdx];
    }

    // Stat choice dropdown (deduplicated by name, alphabetical)
    {
        auto& uniqueStats = ItemDb::Instance().GetUniqueStatChoices();
        if (!uniqueStats.empty())
        {
            bool statSelected = m_selectedStatChoiceIdx > 0;
            const char* statPreview = statSelected
                ? (m_selectedStatChoiceIdx > 0 && m_selectedStatChoiceIdx <= (int)uniqueStats.size()
                    ? uniqueStats[m_selectedStatChoiceIdx - 1].second.c_str()
                    : "Any Stat")
                : "Any Stat";

            if (ImGui::BeginCombo("Stat", statPreview))
            {
                bool isSelected = (m_selectedStatChoiceIdx == 0);
                if (ImGui::Selectable("Any Stat", isSelected))
                {
                    m_selectedStatChoiceIdx = 0;
                    m_selectedStatChoiceRepId = 0;
                    m_options.StatChoiceIds.clear();
                }
                int idx = 1;
                for (auto& [id, name] : uniqueStats)
                {
                    isSelected = (m_selectedStatChoiceIdx == idx);
                    if (ImGui::Selectable(name.c_str(), isSelected))
                    {
                        m_selectedStatChoiceIdx = idx;
                        m_selectedStatChoiceRepId = id;
                        m_options.StatChoiceIds = ItemDb::Instance().GetStatIdsByName(name);
                    }
                    idx++;
                }
                ImGui::EndCombo();
            }
        }
    }

    if (ImGui::Button("Clear Filters"))
    {
        m_options = SearchOptions();
        m_selectedTypeIdx = 0;
        m_selectedRarityIdx = 0;
        m_selectedSubTypeIdx = 0;
        m_subTypes.clear();
        m_selectedStatChoiceIdx = 0;
        m_selectedStatChoiceRepId = 0;
    }

    ImGui::SameLine();
    const char* groupingItems = "By Location\0By Location (Merged)\0Merged\0";
    ImGui::Combo("Grouping", &m_options.StackGrouping, groupingItems);

    ImGui::EndChild();
}

void MainWindow::RenderItemIcon(const InventoryItem& item)
{
    const float iconSize = 50.0f;
    const float padding = 2.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    ImU32 borderColor = RarityToColor(item.ItemInfo ? item.ItemInfo->Rarity : ItemRarity::Basic);
    ImU32 bgColor = IM_COL32(30, 30, 30, 255);

    ImVec2 min = cursorPos;
    ImVec2 max = ImVec2(min.x + iconSize, min.y + iconSize);

    drawList->AddRectFilled(min, max, bgColor);
    drawList->AddRect(min, max, borderColor, 0.0f, 0, 2.0f);

    // Try to load and display icon
    if (item.ItemInfo && !item.ItemInfo->IconUrl.empty())
    {
        Texture_t* tex = IconCache::Instance().GetIcon(item.Id, item.ItemInfo->IconUrl);
        if (tex && tex->Resource)
        {
            ImGui::Image(tex->Resource, ImVec2(iconSize - 4, iconSize - 4));
        }
        else
        {
            ImGui::Dummy(ImVec2(iconSize, iconSize));
        }
    }
    else
    {
        ImGui::Dummy(ImVec2(iconSize, iconSize));
    }

    // Count overlay
    if (item.Count > 1)
    {
        std::string countStr = std::to_string(item.Count);
        ImVec2 textSize = ImGui::CalcTextSize(countStr.c_str());
        drawList->AddText(
            ImVec2(max.x - textSize.x - 2, min.y + 2),
            IM_COL32(255, 255, 224, 255),
            countStr.c_str()
        );
    }

    // Tooltip on hover
    if (ImGui::IsItemHovered())
    {
        RenderItemTooltip(item);
    }

    // Context menu on right-click
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Copy Name"))
        {
            if (item.ItemInfo)
            {
                const char* output = item.ItemInfo->Name.c_str();
                ImGui::SetClipboardText(output);
            }
        }
        if (ImGui::MenuItem("Copy Chat Code"))
        {
            if (item.ItemInfo)
            {
                const char* output = item.ItemInfo->ChatCode.c_str();
                ImGui::SetClipboardText(output);
            }
        }

        ImGui::Separator();

        auto& links = ItemDb::Instance().GetExternalLinks();
        for (auto& link : links)
        {
            if (ImGui::MenuItem(link.Name.c_str()))
            {
                std::string url = link.Url;
                auto info = item.ItemInfo;
                if (info)
                {
                    auto pos = url.find("@@name@@");
                    if (pos != std::string::npos)
                        url.replace(pos, 8, info->Name);

                    pos = url.find("@@chat@@");
                    if (pos != std::string::npos)
                        url.replace(pos, 8, info->ChatCode);

                    pos = url.find("@@itemid@@");
                    if (pos != std::string::npos)
                        url.replace(pos, 10, std::to_string(item.Id));
                }
                ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();
}

void MainWindow::RenderItemTooltip(const InventoryItem& item)
{
    ImGui::BeginTooltip();

    if (item.ItemInfo)
    {
        ImU32 nameColor = RarityToColor(item.ItemInfo->Rarity);
        ImGui::TextColored(ImVec4(
            ((nameColor >> 0) & 0xFF) / 255.0f,
            ((nameColor >> 8) & 0xFF) / 255.0f,
            ((nameColor >> 16) & 0xFF) / 255.0f,
            1.0f
        ), "%s", item.ItemInfo->Name.c_str());
    }
    else
    {
        ImGui::Text("Item #%d", item.Id);
    }

    // Source
    if (item.Source == InventoryItemSource::CharacterInventory || item.Source == InventoryItemSource::CharacterEquipment)
    {
        ImGui::Text("Location: %s (%s)", ItemSourceToString(item.Source), item.CharacterName.c_str());
    }
    else
    {
        ImGui::Text("Location: %s", ItemSourceToString(item.Source));
    }

    if (!item.StatAttributes.empty())
    {
        ImGui::Separator();
        const char* statName = ItemDb::Instance().GetStatChoiceName(item.StatChoiceId);
        if (statName)
            ImGui::Text("Stats (%s):", statName);
        else
            ImGui::TextUnformatted("Stats:");
        for (auto& [attr, val] : item.StatAttributes)
        {
            ImGui::Text("  %s: +%d", attr.c_str(), val);
        }
    }

    // Upgrades
    if (!item.Upgrades.empty())
    {
        ImGui::Separator();
        for (int upgradeId : item.Upgrades)
        {
            StaticItemInfo* upg = ItemDb::Instance().GetItemInfo(upgradeId);
            if (upg)
            {
                ImU32 upgColor = RarityToColor(upg->Rarity);
                ImGui::TextColored(ImVec4(
                    ((upgColor >> 0) & 0xFF) / 255.0f,
                    ((upgColor >> 8) & 0xFF) / 255.0f,
                    ((upgColor >> 16) & 0xFF) / 255.0f,
                    1.0f
                ), "%s", upg->Name.c_str());
            }
        }
    }

    // Parent item
    if (item.ParentItemId > 0)
    {
        StaticItemInfo* parent = ItemDb::Instance().GetItemInfo(item.ParentItemId);
        if (parent)
        {
            ImGui::Separator();
            ImGui::Text("Contained in: %s", parent->Name.c_str());
        }
    }

    ImGui::EndTooltip();
}

void MainWindow::RenderResults()
{
    if (m_searching)
    {
        ImGui::Text("Searching...");
        return;
    }

    if (m_results.empty())
    {
        if (!m_searchQuery.empty() || m_options.HasActiveFilter())
        {
            ImGui::Text("No items found matching your search.");
        }
        else
        {
            ImGui::Text("Type to search items, or use Filters to browse your inventory.");
        }
        return;
    }

    // Group by source
    struct GroupInfo
    {
        std::string Label;
        ImU32 LabelColor;
        std::vector<InventoryItem> Items;
    };

    std::vector<GroupInfo> groups;

    // First pass: collect sources
    std::unordered_map<std::string, std::vector<InventoryItem>> groupMap;
    for (auto& item : m_results)
    {
        std::string key;
        if (item.Source == InventoryItemSource::CharacterInventory || item.Source == InventoryItemSource::CharacterEquipment)
        {
            key = "Character: " + item.CharacterName;
        }
        else
        {
            key = ItemSourceToString(item.Source);
        }
        groupMap[key].push_back(item);
    }

    // Sort groups: non-character first, then characters
    for (auto& [label, items] : groupMap)
    {
        if (label.find("Character:") != 0)
        {
            groups.push_back({ label, IM_COL32(255, 255, 0, 255), std::move(items) });
        }
    }
    for (auto& [label, items] : groupMap)
    {
        if (label.find("Character:") == 0)
        {
            groups.push_back({ label, IM_COL32(255, 255, 255, 255), std::move(items) });
        }
    }

    // Render each group
    ImGui::BeginChild("##results_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& group : groups)
    {
        if (ImGui::CollapsingHeader(group.Label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            int itemsInRow = 0;
            for (auto& item : group.Items)
            {
                if (itemsInRow > 0 && itemsInRow % 8 != 0)
                {
                    ImGui::SameLine();
                }
                else if (itemsInRow > 0)
                {
                    ImGui::NewLine();
                }

                RenderItemIcon(item);
                itemsInRow++;
            }

            ImGui::Unindent(10.0f);
            ImGui::NewLine();
        }
    }

    ImGui::EndChild();
}

void MainWindow::PerformSearch(const std::string& query)
{
    m_searching = true;
    LogDebug(("PerformSearch: '" + query + "'").c_str());

    auto matchedIds = ItemDb::Instance().SearchByName(query);
    LogDebug(("PerformSearch: SearchByName returned " + std::to_string(matchedIds.size()) + " IDs").c_str());

    auto matchedItems = PlayerItems::Instance().FindMatches(matchedIds, m_hideLegendaryArmory, m_hideEquippedBags);
    LogDebug(("PerformSearch: FindMatches returned " + std::to_string(matchedItems.size()) + " items").c_str());

    // Apply filters
    if (m_options.HasActiveFilter())
    {
        LogDebug("PerformSearch: applying filters");
        std::vector<InventoryItem> filtered;
        for (auto& item : matchedItems)
        {
            if (!m_options.FilterItem(item.ItemInfo))
                continue;
            if (!m_options.StatChoiceIds.empty())
            {
                bool match = false;
                for (int id : m_options.StatChoiceIds)
                {
                    if (item.StatChoiceId == id) { match = true; break; }
                }
                if (!match) continue;
            }
            filtered.push_back(item);
        }
        LogDebug(("PerformSearch: filters reduced to " + std::to_string(filtered.size()) + " items").c_str());
        m_results = std::move(filtered);
    }
    else
    {
        m_results = std::move(matchedItems);
    }

    m_searching = false;
    LogDebug(("PerformSearch: final result count = " + std::to_string(m_results.size())).c_str());
}

void MainWindow::PerformBrowse()
{
    if (!m_options.HasActiveFilter())
    {
        m_results.clear();
        return;
    }

    m_searching = true;

    auto matchedIds = ItemDb::Instance().Browse(m_options);
    auto matchedItems = PlayerItems::Instance().FindMatches(matchedIds, m_hideLegendaryArmory, m_hideEquippedBags);

    if (!m_options.StatChoiceIds.empty())
    {
        std::vector<InventoryItem> filtered;
        for (auto& item : matchedItems)
        {
            bool match = false;
            for (int id : m_options.StatChoiceIds)
            {
                if (item.StatChoiceId == id) { match = true; break; }
            }
            if (match)
                filtered.push_back(item);
        }
        m_results = std::move(filtered);
    }
    else
    {
        m_results = std::move(matchedItems);
    }

    m_searching = false;
}

void MainWindow::RenderSetupWizard()
{
    ImGui::SetNextWindowSize(ImVec2(520, 440), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(
        ImGui::GetIO().DisplaySize.x * 0.5f,
        ImGui::GetIO().DisplaySize.y * 0.5f
    ), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::Begin("Item Search Setup", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::TextWrapped("Welcome to Gw2ItemSearch!");
    ImGui::Separator();
    ImGui::TextWrapped("To get started, enter your Guild Wars 2 API key and choose how item data should be loaded.");

    ImGui::NewLine();

    // API key
    ImGui::TextUnformatted("API Key:");
    ImGui::InputText("##wizard_api_key", m_apiKeyBuffer, sizeof(m_apiKeyBuffer));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create a free API key at https://account.arena.net/applications");
    ImGui::NewLine();

    // Fetch mode selection
    ImGui::TextUnformatted("Item data mode:");
    ImGui::TextDisabled("How should item names, icons, and types be loaded?");

    const char* modeNames[] = {
        "Create Database (Sequential) \xe2\x80\x94 ~6 minutes, no lag",
        "Create Database (Parallel) \xe2\x80\x94 ~1 minute, some in-game lag",
        "On Demand \xe2\x80\x94 No initial wait, each search takes ~2 seconds"
    };
    const char* modeDescriptions[] = {
        "Downloads ALL 70,000+ items from the GW2 API one batch at a time. "
        "After the first load, subsequent loads are instant from disk cache.",
        "Downloads the full database using 6 concurrent connections. "
        "Much faster but may cause stutter while the game is running.",
        "No full download. Item names are fetched from the API each time you search. "
        "Your inventory items are cached locally. Best if you don't want a long first load."
    };
    const char* modeWarnings[] = {
        "",
        "",
        "Warning: With on-demand mode, every search makes an API request (~1-2 seconds) "
        "and the stat/dropdown-based 'Browse' feature is unavailable."
    };

    for (int i = 0; i < 3; i++)
    {
        if (ImGui::RadioButton(modeNames[i], &m_wizardFetchMode, i))
        {
        }
        ImGui::TextDisabled("  %s", modeDescriptions[i]);
        if (i == 2 && m_wizardFetchMode == 2)
        {
            ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "  %s", modeWarnings[i]);
        }
        ImGui::NewLine();
    }

    ImGui::Separator();

    bool hasKey = strlen(m_apiKeyBuffer) > 0;
    if (!hasKey)
    {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Please enter an API key to continue.");
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::Button("Save and Start", ImVec2(200, 0));
        ImGui::PopStyleVar();
    }
    else if (ImGui::Button("Save and Start", ImVec2(200, 0)))
    {
        FetchMode mode = static_cast<FetchMode>(m_wizardFetchMode);
        SaveFetchMode(mode);
        SaveSettingsApiKey(m_apiKeyBuffer);
        Gw2Api::Instance().SetApiKey(m_apiKeyBuffer);

        ItemDb::Instance().SetFetchMode(mode);
        LogInfo("Starting data load from setup wizard");

        std::string cacheDir = m_cacheDir;
        std::thread([mode, cacheDir]()
        {
            try
            {
                if (mode != FetchMode::OnDemand)
                {
                    if (mode == FetchMode::CreateDBParallel)
                        ItemDb::Instance().UpdateFromApiParallel();
                    else
                        ItemDb::Instance().UpdateFromApi();
                }
                PlayerItems::Instance().Initialize(Gw2Api::Instance().GetApiKey(), cacheDir);
            }
            catch (const std::exception& e)
            {
                LogWarn(("Data loading failed: " + std::string(e.what())).c_str());
            }
            catch (...)
            {
                LogWarn("Data loading failed with unknown exception");
            }
        }).detach();

        m_showSetupWizard = false;
        m_visible = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)))
    {
        m_showSetupWizard = false;
    }

    ImGui::End();
}

void MainWindow::RenderSettingsWindow()
{
    ImGui::Begin("Item Search Settings", &m_showSettings);

    if (ImGui::BeginTabBar("SettingsTabs"))
    {
            if (ImGui::BeginTabItem("Options"))
            {
                static bool prevHideLegendaryArmory = m_hideLegendaryArmory;
                static bool prevHideEquippedBags = m_hideEquippedBags;
                static bool prevAutoFocusSearch = m_autoFocusSearch;

                ImGui::Checkbox("Hide Legendary Armory", &m_hideLegendaryArmory);
                ImGui::Checkbox("Hide Equipped Bags", &m_hideEquippedBags);
                ImGui::Checkbox("Auto-focus search field", &m_autoFocusSearch);

                if (m_hideLegendaryArmory != prevHideLegendaryArmory ||
                    m_hideEquippedBags != prevHideEquippedBags ||
                    m_autoFocusSearch != prevAutoFocusSearch)
                {
                    SaveSettingsOptions(m_hideLegendaryArmory, m_hideEquippedBags, m_autoFocusSearch);
                    prevHideLegendaryArmory = m_hideLegendaryArmory;
                    prevHideEquippedBags = m_hideEquippedBags;
                    prevAutoFocusSearch = m_autoFocusSearch;
                }

                ImGui::Separator();

                // Fetch mode selector
                FetchMode currentMode = ItemDb::Instance().GetFetchMode();
                int modeIdx = static_cast<int>(currentMode);
                const char* modeItems = "Create Database (Sequential)\0Create Database (Parallel)\0On Demand\0";
                ImGui::TextUnformatted("Item data mode:");
                if (ImGui::Combo("##fetch_mode", &modeIdx, modeItems))
                {
                    FetchMode newMode = static_cast<FetchMode>(modeIdx);
                    SaveFetchMode(newMode);
                    ItemDb::Instance().SetFetchMode(newMode);
                }
                if (ImGui::IsItemHovered())
                {
                    if (modeIdx == 0)
                        ImGui::SetTooltip("Downloads all 70k+ items sequentially. ~6 min first load, no lag, instant searches.");
                    else if (modeIdx == 1)
                        ImGui::SetTooltip("Downloads all 70k+ items using 6 parallel connections. ~1 min first load, minor lag.");
                    else
                        ImGui::SetTooltip("No database. Item names fetched via API on each search (~2 sec). Choose this to skip the long first load.");
                }

                if (modeIdx == 2)
                {
                    ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "Each search will take approximately 2 seconds (API request).");
                }

                ImGui::Separator();
                ImGui::Text("API Key");
                ImGui::InputText("##api_key_settings", m_apiKeyBuffer, sizeof(m_apiKeyBuffer));
                if (ImGui::Button("Save and Reload"))
                {
                    std::string key(m_apiKeyBuffer);
                    SaveSettingsApiKey(key);
                    Gw2Api::Instance().SetApiKey(key);
                    FetchMode fetchMode = ItemDb::Instance().GetFetchMode();
                    std::string cacheDir = m_cacheDir;
                    std::thread([fetchMode, cacheDir]()
                    {
                        try
                        {
                            if (fetchMode != FetchMode::OnDemand)
                            {
                                if (fetchMode == FetchMode::CreateDBParallel)
                                    ItemDb::Instance().UpdateFromApiParallel();
                                else
                                    ItemDb::Instance().UpdateFromApi();
                            }
                            PlayerItems::Instance().Initialize(Gw2Api::Instance().GetApiKey(), cacheDir);
                        }
                        catch (const std::exception& e)
                        {
                            LogWarn(("Data loading failed: " + std::string(e.what())).c_str());
                        }
                        catch (...)
                        {
                            LogWarn("Data loading failed with unknown exception");
                        }
                    }).detach();
                }

                ImGui::EndTabItem();
            }

        if (ImGui::BeginTabItem("About"))
        {
            ImGui::Text("Gw2ItemSearch v1.0.0");
            ImGui::Separator();
            ImGui::Text("Originally by Pentose (tentose)");
            if (ImGui::SmallButton("github.com/tentose/Gw2ItemSearch"))
                ShellExecuteA(nullptr, "open", "https://github.com/tentose/Gw2ItemSearch", nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::Text("Ported to Nexus addon by Todd0042");
            if (ImGui::SmallButton("github.com/Todd0042/item-search-nexus"))
                ShellExecuteA(nullptr, "open", "https://github.com/Todd0042/item-search-nexus", nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::Separator();
            ImGui::Text("Licensed under MIT.");
            ImGui::Separator();
            ImGui::Text("Searches for items across your account.");
            ImGui::Text("Finds items in bank, shared inventory,");
            ImGui::Text("material storage, character bags,");
            ImGui::Text("equipment tabs, and trading post.");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
