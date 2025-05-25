#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "GameObject.h"
#include "Map.h"
#include "Bag.h"
#include "Item.h"

#define SPELL_SICKNESS 15007
#define GOB_CHEST 179697 // Heavy Junkbox

// Helper funkce: vrací true, pokud gear smí dropnout
bool CanDropLoot(Player* killer, Player* killed)
{
    // self-kill, stejné IP, sebevražda
    if (!killer || !killed) return false;
    if (killer->GetGUID() == killed->GetGUID()) return false;
    if (killer->GetSession()->GetRemoteAddress() == killed->GetSession()->GetRemoteAddress()) return false;
    // aura sickness nebo 5+ lvl rozdíl
    if (killed->HasAura(SPELL_SICKNESS)) return false;
    if (killer->GetLevel() - 5 >= killed->GetLevel()) return false;
    // sanctuary
    AreaTableEntry const* area1 = sAreaTableStore.LookupEntry(killer->GetAreaId());
    AreaTableEntry const* area2 = sAreaTableStore.LookupEntry(killed->GetAreaId());
    if ((area1 && area1->IsSanctuary()) || (area2 && area2->IsSanctuary())) return false;
    // BG/arena ochrana na obou stranách
    if (killer->InBattleground() || killer->InArena() || killed->InBattleground() || killed->InArena()) return false;
    // všechno ok
    return true;
}

class HighRiskSystem : public PlayerScript
{
public:
    HighRiskSystem() : PlayerScript("HighRiskSystem") {}

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        // Early return: vůbec nevyhodnocuj v BG/aréně
        if (killer->InBattleground() || killer->InArena() || killed->InBattleground() || killed->InArena())
            return;

        if (!CanDropLoot(killer, killed)) {
            return;
        }

        // Drop chance z configu
        uint32 dropChance = sConfigMgr->GetOption<int>("HighRiskSystem.DropChance", 70);
        if (!roll_chance_i(dropChance)) {
//             printf("HighRiskSystem: Drop failed %u%% chance\n", dropChance);
            return;
        }

        if (!killed->IsAlive()) // player už je mrtvý
        {
            uint32 maxItems = sConfigMgr->GetOption<int>("HighRiskSystem.MaxItems", 2);
            uint32 count = 0;

            // spawn chesty
            GameObject* go = killer->SummonGameObject(GOB_CHEST, killed->GetPositionX(), killed->GetPositionY(), killed->GetPositionZ(), killed->GetOrientation(), 0, 0, 0, 0, 300);
            if (!go)
            {
//                 printf("HighRiskSystem: Chest spawn failed!\n");
                return;
            }
            go->SetOwnerGUID(ObjectGuid::Empty);
            go->loot.clear();
            go->SetGoState(GO_STATE_READY);
            go->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE | GO_FLAG_IN_USE | GO_FLAG_DESTROYED | GO_FLAG_INTERACT_COND);
            go->SetRespawnTime(300);

            // --- EQUIPPED ITEMS ---
            for (uint8 i = 0; i < EQUIPMENT_SLOT_END && count < maxItems; ++i)
            {
                if (Item* item = killed->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    if (item->GetTemplate()->Quality >= ITEM_QUALITY_UNCOMMON)
                    {
                        std::string itemName = item->GetTemplate()->Name1;
                        std::string msg = "|cffDA70D6You have lost your " + itemName;
                        ChatHandler(killed->GetSession()).SendSysMessage(msg.c_str());
                        go->loot.AddItem(LootStoreItem(item->GetEntry(), 0, 100, 0, LOOT_MODE_DEFAULT, 0, 1, 1));
                        killed->DestroyItem(INVENTORY_SLOT_BAG_0, item->GetSlot(), true);
                        count++;
                    }
                }
            }
            // --- INVENTORY ---
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END && count < maxItems; ++i)
            {
                if (Item* item = killed->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    if (item->GetTemplate()->Quality >= ITEM_QUALITY_UNCOMMON)
                    {
                        std::string itemName = item->GetTemplate()->Name1;
                        std::string msg = "|cffDA70D6You have lost your " + itemName;
                        ChatHandler(killed->GetSession()).SendSysMessage(msg.c_str());
                        go->loot.AddItem(LootStoreItem(item->GetEntry(), 0, 100, 0, LOOT_MODE_DEFAULT, 0, 1, 1));
                        killed->DestroyItemCount(item->GetEntry(), item->GetCount(), true, false);
                        count++;
                    }
                }
            }
            // --- BAGS ---
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
                                std::string msg = "|cffDA70D6You have lost your " + itemName;
                                ChatHandler(killed->GetSession()).SendSysMessage(msg.c_str());
                                go->loot.AddItem(LootStoreItem(item->GetEntry(), 0, 100, 0, LOOT_MODE_DEFAULT, 0, 1, 1));
                                killed->DestroyItemCount(item->GetEntry(), item->GetCount(), true, false);
                                count++;
                            }
                        }
                    }
                }
            }
//             printf("HighRiskSystem: Chest loot count: %zu\n", go->loot.items.size());
        }
    }
};

void AddSC_HighRiskSystems()
{
    new HighRiskSystem();
}
