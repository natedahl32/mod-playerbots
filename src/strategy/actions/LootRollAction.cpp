/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LootRollAction.h"

#include "Event.h"
#include "Group.h"
#include "ItemUsageValue.h"
#include "LootAction.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

bool LootRollAction::Execute(Event event)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    std::vector<Roll*> rolls = group->GetRolls();
    for (Roll*& roll : rolls)
    {
        if (roll->playerVote.find(bot->GetGUID())->second != NOT_EMITED_YET)
        {
            continue;
        }
        ObjectGuid guid = roll->itemGUID;
        uint32 slot = roll->itemSlot;
        uint32 itemId = roll->itemid;
        int32 randomProperty = 0;
        if (roll->itemRandomPropId)
            randomProperty = roll->itemRandomPropId;
        else if (roll->itemRandomSuffix)
            randomProperty = -((int)roll->itemRandomSuffix);

        RollVote vote = PASS;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;
        
        std::string itemUsageParam;
        if (randomProperty != 0) {
            itemUsageParam = std::to_string(itemId) + "," + std::to_string(randomProperty);
        } else {
            itemUsageParam = std::to_string(itemId);
        }
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemUsageParam);
        
        // Armor Tokens are classed as MISC JUNK (Class 15, Subclass 0), luckily no other items I found have class bits and epic quality.
        if (proto->Class == ITEM_CLASS_MISC && proto->SubClass == ITEM_SUBCLASS_JUNK && proto->Quality == ITEM_QUALITY_EPIC)
        {
            if (CanBotUseToken(proto, bot))
            {
                vote = NEED; // Eligible for "Need"
            }
            else
            {
                vote = GREED; // Not eligible, so "Greed"
            }
        }
        else
        {
            switch (proto->Class)
            {
                case ITEM_CLASS_WEAPON:
                case ITEM_CLASS_ARMOR:
                    if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
                    {
                        vote = NEED;
                    }
                    else if (usage != ITEM_USAGE_NONE)
                    {
                        vote = GREED;
                    }
                    break;
                default:
                    if (StoreLootAction::IsLootAllowed(itemId, botAI))
                        vote = CalculateRollVote(proto); // Ensure correct Need/Greed behavior
                    break;
            }
        }
        if (sPlayerbotAIConfig->lootRollLevel == 0)
        {
            vote = PASS;
        }
        else if (sPlayerbotAIConfig->lootRollLevel == 1)
        {
            if (vote == NEED)
            {
                if (RollUniqueCheck(proto, bot))
                    {
                        vote = PASS;
                    }
                else 
                    {
                        vote = GREED;
                    }
            }
            else if (vote == GREED)
            {
                vote = PASS;
            }
        }
        switch (group->GetLootMethod())
        {
            case MASTER_LOOT:
            case FREE_FOR_ALL:
                group->CountRollVote(bot->GetGUID(), guid, PASS);
                break;
            default:
                group->CountRollVote(bot->GetGUID(), guid, vote);
                break;
        }
        // One item at a time
        return true;
    }

    return false;
}


RollVote LootRollAction::CalculateRollVote(ItemTemplate const* proto)
{
    std::ostringstream out;
    out << proto->ItemId;
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());

    RollVote needVote = PASS;
    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_GUILD_TASK:
        case ITEM_USAGE_BAD_EQUIP:
            needVote = NEED;
            break;
        case ITEM_USAGE_SKILL:
        case ITEM_USAGE_USE:
        case ITEM_USAGE_DISENCHANT:
        case ITEM_USAGE_AH:
        case ITEM_USAGE_VENDOR:
            needVote = GREED;
            break;
        default:
            break;
    }

    return StoreLootAction::IsLootAllowed(proto->ItemId, GET_PLAYERBOT_AI(bot)) ? needVote : PASS;
}

bool MasterLootRollAction::isUseful() { return !botAI->HasActivePlayerMaster(); }

bool MasterLootRollAction::Execute(Event event)
{
    Player* bot = QueryItemUsageAction::botAI->GetBot();

    WorldPacket p(event.getPacket());  // WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid creatureGuid;
    uint32 mapId;
    uint32 itemSlot;
    uint32 itemId;
    uint32 randomSuffix;
    uint32 randomPropertyId;
    uint32 count;
    uint32 timeout;

    p.rpos(0);              // reset packet pointer
    p >> creatureGuid;      // creature guid what we're looting
    p >> mapId;             /// 3.3.3 mapid
    p >> itemSlot;          // the itemEntryId for the item that shall be rolled for
    p >> itemId;            // the itemEntryId for the item that shall be rolled for
    p >> randomSuffix;      // randomSuffix
    p >> randomPropertyId;  // item random property ID
    p >> count;             // items in stack
    p >> timeout;           // the countdown time to choose "need" or "greed"

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    RollVote vote = CalculateRollVote(proto);
    group->CountRollVote(bot->GetGUID(), creatureGuid, CalculateRollVote(proto));

    return true;
}

bool CanBotUseToken(ItemTemplate const* proto, Player* bot)
{
    // Get the bitmask for the bot's class
    uint32 botClassMask = (1 << (bot->getClass() - 1));

    // Check if the bot's class is allowed to use the token
    if (proto->AllowableClass & botClassMask)
    {
        return true; // Bot's class is eligible to use this token
    }

    return false; // Bot's class cannot use this token
}

bool RollUniqueCheck(ItemTemplate const* proto, Player* bot)
{
    // Count the total number of the item (equipped + in bags)
    uint32 totalItemCount = bot->GetItemCount(proto->ItemId, true);

    // Count the number of the item in bags only
    uint32 bagItemCount = bot->GetItemCount(proto->ItemId, false);

    // Determine if the unique item is already equipped
    bool isEquipped = (totalItemCount > bagItemCount);
    if (isEquipped && proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE))
    {
        return true;  // Unique Item is already equipped
    }
    else if (proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE) && (bagItemCount > 1))
    {
        return true; // Unique item already in bag, don't roll for it
    }
    return false; // Item is not equipped or in bags, roll for it
}

bool RollAction::Execute(Event event)
{
    std::string link = event.getParam();
    
    if (link.empty())
    {
        bot->DoRandomRoll(0,100);
        return false;
    }
    ItemIds itemIds = chat->parseItems(link);
    if (itemIds.empty())
        return false;
    uint32 itemId = *itemIds.begin();
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
    {
        return false;
    }
    std::string itemUsageParam;
    itemUsageParam = std::to_string(itemId);
    
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemUsageParam);
    switch (proto->Class)
    {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_ARMOR:
        if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
        {
            // We don't want to actually roll anymore, we want an upgrade based system where Bots link current gear
            //bot->DoRandomRoll(0,100);

            // If the item is in a set, get the number of total items in the set and the number of items the bot currently has equipped
            uint32 setid = proto->ItemSet;
            uint32 totalSetItems = 0;
            uint32 currentSetItems = 0;
            ItemSetEntry const* set = sItemSetStore.LookupEntry(setid);
            if (set)
            {
                // Get total number of items in the set
                for (int i = 0; i < MAX_ITEM_SET_ITEMS; ++i)
                {
                    if (set->itemId[i] != 0)
                    {
                        totalSetItems++;
                    }
                }

                // Get number of currently equipped items
                for (uint8 i = 0; i < INVENTORY_SLOT_BAG_END; ++i)
                {
                    Item* tempItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
                    if (tempItem)
                    {
                        if (tempItem->GetTemplate()->ItemSet == setid)
                            currentSetItems++;
                    }
                }
            }

            // When rolling for an item, also link the item being upgraded
            uint8 invType = proto->InventoryType;
            if (invType == INVTYPE_RANGED || invType == INVTYPE_THROWN || invType == INVTYPE_RANGEDRIGHT)
            {
                Item* currentRanged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
                if (currentRanged != NULL)
                {
                    std::ostringstream out;
                    out << "current item " << chat->FormatItem(currentRanged->GetTemplate()) << " in ranged slot";
                    if (set && currentSetItems > 0)
                        out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                    botAI->TellMaster(out);
                    return true;
                }
                else
                {
                    std::ostringstream out;
                    out << "No item currently equipped ";
                    if (set && currentSetItems > 0)
                        out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                    botAI->TellMaster(out);
                    return true;
                }
            }

            // Handle weapon
            bool isWeapon = (proto->Class == ITEM_CLASS_WEAPON);
            if (isWeapon)
            {
                Item* mainHandItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                Item* offHandItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

                if (mainHandItem != NULL || offHandItem != NULL)
                {
                    std::ostringstream out;
                    out << "current item ";
                    if (mainHandItem != NULL)
                        out << " MH " << chat->FormatItem(mainHandItem->GetTemplate());
                    if (offHandItem != NULL)
                        out << " OH " << chat->FormatItem(offHandItem->GetTemplate());
                    if (set && currentSetItems > 0)
                        out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                    botAI->TellMaster(out);
                    return true;
                }
                else
                {
                    std::ostringstream out;
                    out << "No item currently equipped ";
                    if (set && currentSetItems > 0)
                        out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                    botAI->TellMaster(out);
                    return true;
                }
            }

            // Handle ring and trinket by linking both
            uint8 dstSlot = botAI->FindEquipSlot(proto, NULL_SLOT, true);
            if (dstSlot == EQUIPMENT_SLOT_FINGER1 || dstSlot == EQUIPMENT_SLOT_TRINKET1)
            {
                Item* first = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, dstSlot);
                Item* second = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, dstSlot + 1);

                if (first != NULL || second != NULL)
                {
                    std::ostringstream out;
                    out << "current item ";
                    if (first != NULL)
                        out << " " << chat->FormatItem(first->GetTemplate());
                    if (second != NULL)
                        out << " " << chat->FormatItem(second->GetTemplate());
                    if (set && currentSetItems > 0)
                        out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                    botAI->TellMaster(out);
                    return true;
                }
                else
                {
                    std::ostringstream out;
                    out << "No item currently equipped ";
                    if (set && currentSetItems > 0)
                        out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                    botAI->TellMaster(out);
                    return true;
                }
            }

            // Handle everything else
            Item* currentlyEquipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, dstSlot);
            if (currentlyEquipped != NULL)
            {
                std::ostringstream out;
                out << "current item " << chat->FormatItem(currentlyEquipped->GetTemplate());
                if (set && currentSetItems > 0)
                    out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                botAI->TellMaster(out);
            }
            else
            {
                std::ostringstream out;
                out << "No item currently equipped ";
                if (set && currentSetItems > 0)
                    out << " [Set Items (" << currentSetItems << "/" << totalSetItems << ")]";
                botAI->TellMaster(out);
            }
        }
    }
    return true;
}
