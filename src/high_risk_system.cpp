#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "GameObject.h"
#include "Map.h"
#include "Bag.h"
#include "Item.h"
#include <unordered_set>
#include <chrono>

#define SPELL_SICKNESS 15007
#define GOB_CHEST 2843 // Heavy Junkbox

// Uchovává guid hráčů v BG/Arena (highrisk vypnut)
std::unordered_set<uint32_t> highRiskDisabled;

bool CanDropLoot(Player* killer, Player* killed)
{
    if (!killer || !killed) return false;
    // === TADY JE NOVÁ KONTROLA: Pokud je killer NEBO killed v BG/Areně, nedroppovat loot ===
    if (killer->GetMap()->IsBattlegroundOrArena() || killed->GetMap()->IsBattlegroundOrArena())
        return false;
    if (killer->GetGUID() == killed->GetGUID()) return false;
    if (killer->GetSession()->GetRemoteAddress() == killed->GetSession()->GetRemoteAddress()) return false;
    if (killed->HasAura(SPELL_SICKNESS)) return false;
    if (killer->GetLevel() - 5 >= killed->GetLevel()) return false;

    AreaTableEntry const* area1 = sAreaTableStore.LookupEntry(killer->GetAreaId());
    AreaTableEntry const* area2 = sAreaTableStore.LookupEntry(killed->GetAreaId());
    if ((area1 && area1->IsSanctuary()) || (area2 && area2->IsSanctuary())) return false;

    return true;
}

class HighRiskSystem : public PlayerScript
{
public:
    HighRiskSystem() : PlayerScript("HighRiskSystem") {}

    void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/)
    {
        if (!player)
            return;
        if (player->GetMap()->IsBattlegroundOrArena())
            highRiskDisabled.insert(player->GetGUID().GetCounter());
        else
            highRiskDisabled.erase(player->GetGUID().GetCounter());
    }

    void OnLogout(Player* player)
    {
        if (!player)
            return;
        highRiskDisabled.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerPVPKill(Player* killer, Player* killed)
    {
        if (!killer || !killed)
            return;

        // Pokud je systém vypnutý pro killer nebo killed (BG/Arena), skončit
        if (highRiskDisabled.count(killer->GetGUID().GetCounter()) || highRiskDisabled.count(killed->GetGUID().GetCounter()))
            return;

        if (!CanDropLoot(killer, killed))
            return;

        uint32 dropChance = sConfigMgr->GetOption<int>("HighRiskSystem.DropChance", 70);
        if (!roll_chance_i(dropChance))
            return;

        if (!killed->IsAlive())
        {
            uint32 maxItems = sConfigMgr->GetOption<int>("HighRiskSystem.MaxItems", 2);
            uint32 chestDespawnMs = sConfigMgr->GetOption<int>("HighRiskSystem.ChestDespawnMs", 15000);
            uint32 count = 0;

            GameObject* go = killer->SummonGameObject(GOB_CHEST, killed->GetPositionX(), killed->GetPositionY(), killed->GetPositionZ(), killed->GetOrientation(), 0, 0, 0, 0, 0);
            if (!go)
                return;

            go->SetOwnerGUID(ObjectGuid::Empty);
            go->loot.clear();
            go->SetGoState(GO_STATE_READY);
            go->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE | GO_FLAG_IN_USE | GO_FLAG_DESTROYED | GO_FLAG_INTERACT_COND);
            go->SetLootState(GO_READY);

            // Debug
            if (killer && killer->GetSession())
                ChatHandler(killer->GetSession()).SendSysMessage("Debug: Chest despawn za X ms");

            // EQUIPPED ITEMS
            for (uint8 i = 0; i < EQUIPMENT_SLOT_END && count < maxItems; ++i)
            {
                if (Item* item = killed->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    if (item->GetTemplate()->Quality >= ITEM_QUALITY_UNCOMMON)
                    {
                        std::string itemName = item->GetTemplate()->Name1;
                        ChatHandler(killed->GetSession()).PSendSysMessage("|cffDA70D6You have lost your %s", itemName.c_str());
                        ChatHandler(killer->GetSession()).PSendSysMessage("|cff00FF00You looted %s from your victim!", itemName.c_str());
                        go->loot.AddItem(LootStoreItem(item->GetEntry(), 0, 100, 0, LOOT_MODE_DEFAULT, 0, 1, 1));
                        killed->DestroyItem(INVENTORY_SLOT_BAG_0, item->GetSlot(), true);
                        count++;
                    }
                }
            }

            // INVENTORY
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END && count < maxItems; ++i)
            {
                if (Item* item = killed->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    if (item->GetTemplate()->Quality >= ITEM_QUALITY_UNCOMMON)
                    {
                        std::string itemName = item->GetTemplate()->Name1;
                        ChatHandler(killed->GetSession()).PSendSysMessage("|cffDA70D6You have lost your %s", itemName.c_str());
                        ChatHandler(killer->GetSession()).PSendSysMessage("|cff00FF00You looted %s from your victim!", itemName.c_str());
                        go->loot.AddItem(LootStoreItem(item->GetEntry(), 0, 100, 0, LOOT_MODE_DEFAULT, 0, 1, 1));
                        killed->DestroyItemCount(item->GetEntry(), item->GetCount(), true, false);
                        count++;
                    }
                }
            }

            // BAGS
            for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END && count < maxItems; ++i)
            {
                if (Bag* bag = killed->GetBagByPos(i))
                {
                    for (uint32 j = 0; j < bag->GetBagSize() && count < maxItems; ++j)
                    {
                        if (Item* item = killed->GetItemByPos(i, j))
                        {
                            if (item->GetTemplate()->Quality >= ITEM_QUALITY_UNCOMMON)
                            {
                                std::string itemName = item->GetTemplate()->Name1;
                                ChatHandler(killed->GetSession()).PSendSysMessage("|cffDA70D6You have lost your %s", itemName.c_str());
                                ChatHandler(killer->GetSession()).PSendSysMessage("|cff00FF00You looted %s from your victim!", itemName.c_str());
                                go->loot.AddItem(LootStoreItem(item->GetEntry(), 0, 100, 0, LOOT_MODE_DEFAULT, 0, 1, 1));
                                killed->DestroyItemCount(item->GetEntry(), item->GetCount(), true, false);
                                count++;
                            }
                        }
                    }
                }
            }

            // Despawn truhly po X ms (default 15s)
            go->DespawnOrUnsummon(std::chrono::milliseconds(chestDespawnMs));
        }
    }
};

void AddSC_HighRiskSystems()
{
    new HighRiskSystem();
}

