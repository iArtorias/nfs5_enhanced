#include "parts_catalog.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <utility>

#include "../../core/hook_util.h"
#include "../../core/log.h"
#include "../../game/addresses.h"

namespace nfs5::parts_catalog
{
    namespace
    {
        using namespace game::car_fixes;
        using Part = const std::uint8_t *;
        struct PartList
        {
            Part * begin;
            Part * end;
        };

        struct Group
        {
            std::string_view subtype;
            const char * heading;
            // Name IDs in catalog order. Twin-turbo kits use the same slot.
            std::array<int, 7> tiers;
        };

        constexpr Group groups[] = {
            {"Air Intake",    "AIR INTAKE",    {5148, 5090, 5092, 5094, 5096}},
            {"Exhaust",       "EXHAUST",       {5116, 5118, 5120, 5122}},
            {"Carbs",         "CARBS",         {5085, 5086, 5088}},
            {"Flywheel",      "FLYWHEEL",      {5100, 5102, 5104}},
            {"Throttle Body", "THROTTLE BODY", {5192, 5112}},
            {"Fuel Injector", "FUEL INJECTOR", {5162, 5114}},
            {"Computer Chip", "ECU",           {5152, 5098}},
            {"Intercooler",   "INTERCOOLER",   {5106, 5108, 5110}},
            {"Turbo",         "TURBO",         {5062, 5064, 5072, 5066, 5074, 5068, 5070}},
            {"Supercharger",  "SUPERCHARGER",  {5076, 5078, 5080, 5082, 5084}},
        };

        bool IsEngine(Part part)
        {
            std::uint16_t category;
            std::memcpy(&category, part + PART_MAIN_CAT_OFFSET, sizeof(category));
            return category == 1;
        }

        std::string_view Subtype(Part part)
        {
            const auto * name = reinterpret_cast<const char *>(part + PART_SUBCAT_OFFSET);
            return {name, static_cast<std::size_t>(std::find(name, name + 30, '\0') - name)};
        }

        // Zero retains the game's Engine heading for assemblies and unfamiliar subtypes.
        int GroupNumber(std::string_view subtype)
        {
            for (int i = 0; i < static_cast<int>(std::size(groups)); ++i)
                if (groups[i].subtype == subtype)
                    return i + 1;
            return 0;
        }

        std::pair<int, int> SortKey(Part part)
        {
            if (!IsEngine(part))
                return {static_cast<int>(std::size(groups)) + 1, 0};

            const auto subtype = Subtype(part);
            const int group = GroupNumber(subtype);
            if (!group)
                return {0, subtype == "Engine (Standard)" ? 0 : 1};

            int nameId;
            std::memcpy(&nameId, part + 4, sizeof(nameId));
            const auto & tiers = groups[group - 1].tiers;
            const auto tier = std::find(tiers.begin(), tiers.end(), nameId);
            return {group, static_cast<int>(tier - tiers.begin())};
        }

        void GroupRows(PartList & list)
        {
            if (list.begin == list.end)
                return;
            auto first = std::find_if(list.begin, list.end, IsEngine);
            auto last = list.end;
            while (last != first && !IsEngine(last[-1]))
                --last;
            auto less = [](Part a, Part b) { return SortKey(a) < SortKey(b); };
            if (!std::is_sorted(first, last, less))
                std::stable_sort(first, last, less);
        }

        constexpr game::GameAddr buildAvailable = 0x517D10;
        constexpr game::GameAddr nativeHeading = 0x518D60;
        constexpr game::GameAddr localize = 0x4E06D0;

        void BuildAvailable(PartList * list, void * car, int year)
        {
            reinterpret_cast<void (__cdecl *)(PartList *, void *, int)>(buildAvailable.Get())(list, car, year);
            GroupRows(*list);
        }

        // The shop reconstructs its inventory view on each lookup. All consumers,
        // including buy/sell selection, receive the same permutation of real pointers.
        void InventoryReady(safetyhook::Context & context)
        {
            GroupRows(*reinterpret_cast<PartList *>(context.eax));
        }

        // Parts.InstallList sorts pointers to entries that carry the part and its
        // installed flag. Its comparator receives the parts, then the game swaps
        // the complete entries. The original comparison breaks ties within a tier.
        bool __fastcall SetupPartLess(Part left, void *, Part right)
        {
            if (IsEngine(left) && IsEngine(right))
            {
                const auto a = SortKey(left);
                const auto b = SortKey(right);
                if (a != b)
                    return a < b;
            }
            using Compare = bool (__thiscall *)(Part, Part);
            return reinterpret_cast<Compare>(game::GameAddr(0x49C350).Get())(left, right);
        }

        int Heading(Part part)
        {
            if (IsEngine(part))
            {
                const int group = GroupNumber(Subtype(part));
                if (group)
                    return -group;
            }
            return reinterpret_cast<int (__cdecl *)(Part)>(nativeHeading.Get())(part);
        }

        const char * HeadingText(int id)
        {
            if (id < 0 && id >= -static_cast<int>(std::size(groups)))
                return groups[-id - 1].heading;
            return reinterpret_cast<const char * (__cdecl *)(int)>(localize.Get())(id);
        }

        struct Call
        {
            game::GameAddr site;
            const char * signature;
            void * replacement;
        };

        // Parts.AllParts compares the current and previous headings both when
        // drawing and when measuring row height. An absent group consumes no row.
        const Call calls[] = {
            {0x5153E8, "E8 23 29 00 00", reinterpret_cast<void *>(&BuildAvailable)},
            {0x5159ED, "E8 6E 33 00 00", reinterpret_cast<void *>(&Heading)},
            {0x5159F9, "E8 62 33 00 00", reinterpret_cast<void *>(&Heading)},
            {0x515A26, "E8 35 33 00 00", reinterpret_cast<void *>(&Heading)},
            {0x515C61, "E8 FA 30 00 00", reinterpret_cast<void *>(&Heading)},
            {0x515C69, "E8 F2 30 00 00", reinterpret_cast<void *>(&Heading)},
            // Car Setup inventory has its own list builder, drawing and row heights.
            {0x5168F7, "E8 54 5A F8 FF", reinterpret_cast<void *>(&SetupPartLess)},
            {0x516984, "E8 D7 23 00 00", reinterpret_cast<void *>(&Heading)},
            {0x51698E, "E8 CD 23 00 00", reinterpret_cast<void *>(&Heading)},
            {0x51699E, "E8 BD 23 00 00", reinterpret_cast<void *>(&Heading)},
            {0x516B1C, "E8 3F 22 00 00", reinterpret_cast<void *>(&Heading)},
            {0x516B26, "E8 35 22 00 00", reinterpret_cast<void *>(&Heading)},
            {0x518E3E, "E8 8D 78 FC FF", reinterpret_cast<void *>(&HeadingText)},
        };
        hook::CallSitePatch patches[std::size(calls)];
        std::unique_ptr<safetyhook::MidHook> inventoryHook;
    }

    void Install()
    {
        for (const auto & call : calls)
            if (!hook::MatchBytes(call.site, call.signature))
            {
                LOG_WARN("Parts catalog grouping unavailable at {:08X}", call.site.Get());
                return;
            }

        inventoryHook = hook::CreateMid("PartsCatalogInventory", game::GameAddr(0x518D57),
                                        "5F 5E 5D 5B 83 C4 14 C3", &InventoryReady);
        if (!inventoryHook)
            return;
        for (std::size_t i = 0; i < std::size(calls); ++i)
            if (!patches[i].Apply("PartsCatalog", calls[i].site, calls[i].replacement))
            {
                Uninstall();
                return;
            }
    }

    void Uninstall()
    {
        for (auto & patch : patches)
            patch.Restore();
        inventoryHook.reset();
    }
}
