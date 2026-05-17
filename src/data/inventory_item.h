#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

enum class ItemRarity
{
    Unknown = 0,
    Junk = 1,
    Basic,
    Fine,
    Masterwork,
    Rare,
    Exotic,
    Ascended,
    Legendary,
};

enum class ItemType
{
    Unknown,
    Armor,
    Back,
    Bag,
    Consumable,
    Container,
    CraftingMaterial,
    Gathering,
    Gizmo,
    Key,
    MiniPet,
    Tool,
    Trait,
    Trinket,
    Trophy,
    UpgradeComponent,
    Weapon,
    PowerCore,
    JadeTechModule,
    Relic,
};

enum class ItemSubType
{
    Unknown = 0,
    Armor_Boots = 100,
    Armor_Coat,
    Armor_Gloves,
    Armor_Helm,
    Armor_HelmAquatic,
    Armor_Leggings,
    Armor_Shoulders,
    Consumable_AppearanceChange = 200,
    Consumable_Booze,
    Consumable_ContractNpc,
    Consumable_Currency,
    Consumable_Food,
    Consumable_Generic,
    Consumable_Halloween,
    Consumable_Immediate,
    Consumable_MountRandomUnlock,
    Consumable_RandomUnlock,
    Consumable_Transmutation,
    Consumable_Unlock,
    Consumable_UpgradeRemoval,
    Consumable_Utility,
    Consumable_TeleportToFriend,
    Container_Default = 300,
    Container_GiftBox,
    Container_Immediate,
    Container_OpenUI,
    Gathering_Foraging = 400,
    Gathering_Logging,
    Gathering_Mining,
    Gathering_Fishing,
    Gathering_Bait,
    Gathering_Lure,
    Gizmo_Default = 500,
    Gizmo_ContainerKey,
    Gizmo_RentableContractNpc,
    Gizmo_UnlimitedConsumable,
    Trinket_Accessory = 600,
    Trinket_Amulet,
    Trinket_Ring,
    UpgradeComponent_Default = 700,
    UpgradeComponent_Gem,
    UpgradeComponent_Rune,
    UpgradeComponent_Sigil,
    Weapon_Axe = 800,
    Weapon_Dagger,
    Weapon_Mace,
    Weapon_Pistol,
    Weapon_Scepter,
    Weapon_Sword,
    Weapon_Focus,
    Weapon_Shield,
    Weapon_Torch,
    Weapon_Warhorn,
    Weapon_Greatsword,
    Weapon_Hammer,
    Weapon_LongBow,
    Weapon_Rifle,
    Weapon_ShortBow,
    Weapon_Staff,
    Weapon_Harpoon,
    Weapon_Speargun,
    Weapon_Trident,
    Weapon_LargeBundle,
    Weapon_SmallBundle,
    Weapon_Toy,
    Weapon_ToyTwoHanded,
};

enum class AttributeName
{
    Power,
    Precision,
    Toughness,
    Vitality,
    ConditionDamage,
    ConditionDuration,
    Healing,
    BoonDuration,
    CritDamage,
    AgonyResistance,
};

enum class InventoryItemSource
{
    Unknown,
    CharacterInventory,
    CharacterEquipment,
    SharedInventory,
    Bank,
    MaterialStorage,
    TradingPostDeliveryBox,
    TradingPostSellOrder,
};

enum class ItemBinding
{
    Unknown,
    Account,
    Character,
};

struct StaticItemInfo
{
    std::string Name;
    std::string IconUrl;
    std::string ChatCode;
    ItemRarity Rarity = ItemRarity::Unknown;
    ItemType Type = ItemType::Unknown;
    ItemSubType SubType = ItemSubType::Unknown;
    int StaticStatChoiceId = 0;
    std::unordered_map<std::string, int> StaticStatAttributes;
};

struct InventoryItem
{
    InventoryItemSource Source = InventoryItemSource::Unknown;
    std::string CharacterName;
    int EquipmentTabId = -1;
    int Id = 0;
    std::string Name;
    int Count = 1;
    int Charges = 0;
    std::vector<int> Infusions;
    std::vector<int> Upgrades;
    int Skin = 0;
    ItemBinding Binding = ItemBinding::Unknown;
    std::string BoundTo;
    int ParentItemId = 0;
    int StatChoiceId = 0;
    std::unordered_map<std::string, int> StatAttributes;

    StaticItemInfo* ItemInfo = nullptr;
};

struct ExternalLink
{
    std::string Url;
    std::string Name;
};

struct SavedSearch
{
    std::string Query;
    std::string TabIconUrl;
    int TypeFilter = 0;
    int SubTypeFilter = 0;
    int RarityFilter = 0;
    int StackGrouping = 0;
};

struct SearchOptions
{
    ItemType Type = ItemType::Unknown;
    ItemSubType SubType = ItemSubType::Unknown;
    ItemRarity Rarity = ItemRarity::Unknown;
    int StackGrouping = 0;
    std::vector<int> StatChoiceIds;

    bool HasActiveFilter() const
    {
        return Type != ItemType::Unknown || SubType != ItemSubType::Unknown
            || Rarity != ItemRarity::Unknown || !StatChoiceIds.empty();
    }

    bool operator==(const SearchOptions& o) const
    {
        return Type == o.Type && SubType == o.SubType && Rarity == o.Rarity
            && StackGrouping == o.StackGrouping && StatChoiceIds == o.StatChoiceIds;
    }

    bool operator!=(const SearchOptions& o) const { return !(*this == o); }

    bool FilterItem(const StaticItemInfo* info) const
    {
        if (!info) return false;
        if (Type != ItemType::Unknown && info->Type != Type) return false;
        if (SubType != ItemSubType::Unknown && info->SubType != SubType) return false;
        if (Rarity != ItemRarity::Unknown && info->Rarity != Rarity) return false;
        return true;
    }
};
