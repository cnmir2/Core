/*
 * Copyright (C) 2011-2014 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2014 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Item.h"
#include "UpdateData.h"
#include "ObjectAccessor.h"
#include "SpellInfo.h"
#include "DB2Stores.h"
#include <vector>

void WorldSession::HandleSplitItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SPLIT_ITEM");

    uint8 srcBag, srcSlot, dstBag, dstSlot;
    uint32 count;

    recvData >> count;
    recvData >> srcSlot >> dstSlot >> dstBag >> srcBag;
    recvData.rfinish();

    // TC_LOG_DEBUG("network", "STORAGE: Received Split item source bag = %u, source slot = %u, dest bag = %u, dest slot = %u, count = %u", srcBag, srcSlot, dstBag, dstSlot, count);

    uint16 src = ((srcBag << 8) | srcSlot);
    uint16 dst = ((dstBag << 8) | dstSlot);

    if (src == dst)
        return;

    if (count == 0)
        return;                                             // Check count - if zero it's fake packet.

    if (!_player->IsValidPos(srcBag, srcSlot, true))
    {
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    if (!_player->IsValidPos(dstBag, dstSlot, false))       // Can be autostore pos.
    {
        _player->SendEquipError(EQUIP_ERR_WRONG_SLOT, NULL, NULL);
        return;
    }

    _player->SplitItem(src, dst, count);
}

void WorldSession::HandleSwapInvItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SWAP_INV_ITEM");

    uint8 srcslot, dstslot;

    recvData >> dstslot >> srcslot;

    // TC_LOG_DEBUG("network", "STORAGE: Received Inventory Item swap: srcslot = %u, dstslot = %u", srcslot, dstslot);

    // prevent attempting to swap same item to current position - cheating.
    if (srcslot == dstslot)
        return;

    if (!_player->IsValidPos(INVENTORY_SLOT_BAG_0, srcslot, true))
    {
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    if (!_player->IsValidPos(INVENTORY_SLOT_BAG_0, dstslot, true))
    {
        _player->SendEquipError(EQUIP_ERR_WRONG_SLOT, NULL, NULL);
        return;
    }

    uint16 src = ((INVENTORY_SLOT_BAG_0 << 8) | srcslot);
    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | dstslot);

    _player->SwapItem(src, dst);
}

void WorldSession::HandleAutoEquipItemSlotOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUTOEQUIP_ITEM_SLOT");

    uint64 itemguid;
    uint8 dstslot;
    recvData >> itemguid >> dstslot;

    // cheating attempt, client should never send opcode in that case
    if (!Player::IsEquipmentPos(INVENTORY_SLOT_BAG_0, dstslot))
        return;

    Item* item = _player->GetItemByGuid(itemguid);
    uint16 dstpos = dstslot | (INVENTORY_SLOT_BAG_0 << 8);

    if (!item || item->GetPos() == dstpos)
        return;

    _player->SwapItem(item->GetPos(), dstpos);
}

void WorldSession::HandleSwapItem(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SWAP_ITEM");

    bool hasSlot[2];
    uint8 dstBag, dstSlot, srcBag, srcSlot, srcSlotAlt, dstSlotAlt;

    recvData >> srcSlotAlt >> dstSlotAlt;

    uint32 count = recvData.ReadBits(2);

    if (count != 2)
        return;

    for (uint8 i = 0; i < count; i++)
    {
        hasSlot[i] = !recvData.ReadBit();
        recvData.ReadBit();                     // has bag (always true?)
    }

    dstSlot = hasSlot[0] ? recvData.read<uint8>() : dstSlotAlt;
    recvData >> dstBag;
    srcSlot = hasSlot[1] ? recvData.read<uint8>() : srcSlotAlt;
    recvData >> srcBag;

    // TC_LOG_DEBUG("network", "STORAGE: Received swap Item: srcbag = %u, srcslot = %u, dstbag = %u, dstslot = %u", srcBag, srcSlot, dstBag, dstSlot);

    uint16 src = ((srcBag << 8) | srcSlot);
    uint16 dst = ((dstBag << 8) | dstSlot);

    // prevent attempting to swap same item to current position - cheating.
    if (src == dst)
        return;

    if (!_player->IsValidPos(srcBag, srcSlot, true))
    {
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    if (!_player->IsValidPos(dstBag, dstSlot, true))
    {
        _player->SendEquipError(EQUIP_ERR_WRONG_SLOT, NULL, NULL);
        return;
    }

    _player->SwapItem(src, dst);
}

void WorldSession::HandleAutoEquipItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUTOEQUIP_ITEM");

    uint8 srcbag, srcslot;

    recvData >> srcslot >> srcbag;
    recvData.rfinish();

    // TC_LOG_DEBUG("network", "STORAGE: Received auto equip Item: source bag = %u, source slot = %u", srcbag, srcslot);

    Item* pSrcItem  = _player->GetItemByPos(srcbag, srcslot);
    if (!pSrcItem)
        return;                                             // Check cheating.

    uint16 dest;
    InventoryResult msg = _player->CanEquipItem(NULL_SLOT, dest, pSrcItem, !pSrcItem->IsBag());
    if (msg != EQUIP_ERR_OK)
    {
        _player->SendEquipError(msg, pSrcItem, NULL);
        return;
    }

    uint16 src = pSrcItem->GetPos();
    if (dest == src)                                           // prevent equip in same slot, only at cheat
        return;

    Item* pDstItem = _player->GetItemByPos(dest);
    if (!pDstItem)                                         // empty slot, simple case
    {
        _player->RemoveItem(srcbag, srcslot, true);
        _player->EquipItem(dest, pSrcItem, true);
        _player->AutoUnequipOffhandIfNeed();
    }
    else                                                    // have currently equipped item, not simple case
    {
        uint8 dstbag = pDstItem->GetBagSlot();
        uint8 dstslot = pDstItem->GetSlot();

        msg = _player->CanUnequipItem(dest, !pSrcItem->IsBag());
        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, pDstItem, NULL);
            return;
        }

        // check dest->src move possibility
        ItemPosCountVec sSrc;
        uint16 eSrc = 0;
        if (_player->IsInventoryPos(src))
        {
            msg = _player->CanStoreItem(srcbag, srcslot, sSrc, pDstItem, true);
            if (msg != EQUIP_ERR_OK)
                msg = _player->CanStoreItem(srcbag, NULL_SLOT, sSrc, pDstItem, true);
            if (msg != EQUIP_ERR_OK)
                msg = _player->CanStoreItem(NULL_BAG, NULL_SLOT, sSrc, pDstItem, true);
        }
        else if (_player->IsBankPos(src))
        {
            msg = _player->CanBankItem(srcbag, srcslot, sSrc, pDstItem, true);
            if (msg != EQUIP_ERR_OK)
                msg = _player->CanBankItem(srcbag, NULL_SLOT, sSrc, pDstItem, true);
            if (msg != EQUIP_ERR_OK)
                msg = _player->CanBankItem(NULL_BAG, NULL_SLOT, sSrc, pDstItem, true);
        }
        else if (_player->IsEquipmentPos(src))
        {
            msg = _player->CanEquipItem(srcslot, eSrc, pDstItem, true);
            if (msg == EQUIP_ERR_OK)
                msg = _player->CanUnequipItem(eSrc, true);
        }

        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, pDstItem, pSrcItem);
            return;
        }

        // now do moves, remove...
        _player->RemoveItem(dstbag, dstslot, false);
        _player->RemoveItem(srcbag, srcslot, false);

        // add to dest
        _player->EquipItem(dest, pSrcItem, true);

        // add to src
        if (_player->IsInventoryPos(src))
            _player->StoreItem(sSrc, pDstItem, true);
        else if (_player->IsBankPos(src))
            _player->BankItem(sSrc, pDstItem, true);
        else if (_player->IsEquipmentPos(src))
            _player->EquipItem(eSrc, pDstItem, true);

        _player->AutoUnequipOffhandIfNeed();
    }
}

void WorldSession::HandleDestroyItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_DESTROY_ITEM");

    int32 count;
    int8 bag, slot;

    recvData >> count;
    recvData >> slot >> bag;

    // TC_LOG_DEBUG("network", "STORAGE: Received destroy Item: bag = %u, slot = %u, count = %u", bag, slot, count);

    uint16 pos = (bag << 8) | slot;

    // prevent drop unequipable items (in combat, for example) and non-empty bags
    if (_player->IsEquipmentPos(pos) || _player->IsBagPos(pos))
    {
        InventoryResult msg = _player->CanUnequipItem(pos, false);
        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, _player->GetItemByPos(pos), NULL);
            return;
        }
    }

    Item* pItem  = _player->GetItemByPos(bag, slot);
    if (!pItem)
    {
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    if (pItem->GetTemplate()->Flags & ITEM_PROTO_FLAG_INDESTRUCTIBLE)
    {
        _player->SendEquipError(EQUIP_ERR_DROP_BOUND_ITEM, NULL, NULL);
        return;
    }

    if (count)
    {
        uint32 i_count = count;
        _player->DestroyItemCount(pItem, i_count, true);
    }
    else
        _player->DestroyItem(bag, slot, true);
}

void WorldSession::HandleReadItem(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_READ_ITEM");

    uint8 bag, slot;
    recvData >> bag >> slot;

    Item* pItem = _player->GetItemByPos(bag, slot);

    if (pItem && pItem->GetTemplate()->PageText)
    {
        WorldPacket data;

        InventoryResult msg = _player->CanUseItem(pItem);
        if (msg == EQUIP_ERR_OK)
        {
            data.Initialize(SMSG_READ_ITEM_OK, 8);
            TC_LOG_INFO("network", "STORAGE: Item page sent");
        }
        else
        {
            data.Initialize(SMSG_READ_ITEM_FAILED, 8);
            TC_LOG_INFO("network", "STORAGE: Unable to read item");
            _player->SendEquipError(msg, pItem, NULL);
        }
        data << pItem->GetGUID();
        SendPacket(&data);
    }
    else
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
}

void WorldSession::HandleSellItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SELL_ITEM");

    ObjectGuid vendorguid, itemguid;
    uint32 count;

    recvData >> count;

    itemguid[7] = recvData.ReadBit();
    vendorguid[0] = recvData.ReadBit();
    vendorguid[3] = recvData.ReadBit();
    itemguid[2] = recvData.ReadBit();
    vendorguid[7] = recvData.ReadBit();
    vendorguid[6] = recvData.ReadBit();
    vendorguid[5] = recvData.ReadBit();
    vendorguid[2] = recvData.ReadBit();
    itemguid[4] = recvData.ReadBit();
    itemguid[6] = recvData.ReadBit();
    itemguid[5] = recvData.ReadBit();
    itemguid[2] = recvData.ReadBit();
    vendorguid[1] = recvData.ReadBit();
    vendorguid[4] = recvData.ReadBit();
    itemguid[0] = recvData.ReadBit();
    itemguid[1] = recvData.ReadBit();

    recvData.ReadByteSeq(vendorguid[6]);
    recvData.ReadByteSeq(vendorguid[2]);
    recvData.ReadByteSeq(itemguid[1]);
    recvData.ReadByteSeq(vendorguid[0]);
    recvData.ReadByteSeq(vendorguid[7]);
    recvData.ReadByteSeq(itemguid[6]);
    recvData.ReadByteSeq(itemguid[0]);
    recvData.ReadByteSeq(itemguid[7]);
    recvData.ReadByteSeq(vendorguid[1]);
    recvData.ReadByteSeq(vendorguid[5]);
    recvData.ReadByteSeq(itemguid[5]);
    recvData.ReadByteSeq(itemguid[3]);
    recvData.ReadByteSeq(itemguid[4]);
    recvData.ReadByteSeq(vendorguid[4]);
    recvData.ReadByteSeq(vendorguid[3]);
    recvData.ReadByteSeq(itemguid[2]);

    if (!itemguid)
        return;

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleSellItemOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(vendorguid)));
        _player->SendSellError(SELL_ERR_CANT_FIND_VENDOR, NULL, itemguid);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    Item* pItem = _player->GetItemByGuid(itemguid);
    if (pItem)
    {
        // prevent sell not owner item
        if (_player->GetGUID() != pItem->GetOwnerGUID())
        {
            _player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, itemguid);
            return;
        }

        // prevent sell non empty bag by drag-and-drop at vendor's item list
        if (pItem->IsNotEmptyBag())
        {
            _player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, itemguid);
            return;
        }

        // prevent sell currently looted item
        if (_player->GetLootGUID() == pItem->GetGUID())
        {
            _player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, itemguid);
            return;
        }

        // prevent selling item for sellprice when the item is still refundable
        // this probably happens when right clicking a refundable item, the client sends both
        // CMSG_SELL_ITEM and CMSG_REFUND_ITEM (unverified)
        if (pItem->HasFlag(ITEM_FIELD_DYNAMIC_FLAGS, ITEM_FLAG_REFUNDABLE))
            return; // Therefore, no feedback to client

        // special case at auto sell (sell all)
        if (count == 0)
            count = pItem->GetCount();
        else
        {
            // prevent sell more items that exist in stack (possible only not from client)
            if (count > pItem->GetCount())
            {
                _player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, itemguid);
                return;
            }
        }

        ItemTemplate const* pProto = pItem->GetTemplate();
        if (pProto)
        {
            if (pProto->SellPrice > 0)
            {
                if (count < pItem->GetCount())               // need split items
                {
                    Item* pNewItem = pItem->CloneItem(count, _player);
                    if (!pNewItem)
                    {
                        TC_LOG_ERROR("network", "WORLD: HandleSellItemOpcode - could not create clone of item %u; count = %u", pItem->GetEntry(), count);
                        _player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, itemguid);
                        return;
                    }

                    pItem->SetCount(pItem->GetCount() - count);
                    _player->ItemRemovedQuestCheck(pItem->GetEntry(), count);
                    if (_player->IsInWorld())
                        pItem->SendUpdateToPlayer(_player);
                    pItem->SetState(ITEM_CHANGED, _player);

                    _player->AddItemToBuyBackSlot(pNewItem);
                    if (_player->IsInWorld())
                        pNewItem->SendUpdateToPlayer(_player);
                }
                else
                {
                    _player->ItemRemovedQuestCheck(pItem->GetEntry(), pItem->GetCount());
                    _player->RemoveItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
                    pItem->RemoveFromUpdateQueueOf(_player);
                    _player->AddItemToBuyBackSlot(pItem);
                }

                uint32 money = pProto->SellPrice * count;
                _player->ModifyMoney(money);
                _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_VENDORS, money);
            }
            else
                _player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, itemguid);
            return;
        }
    }
    _player->SendSellError(SELL_ERR_CANT_FIND_ITEM, creature, itemguid);
    return;
}

void WorldSession::HandleBuybackItem(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_BUYBACK_ITEM");

    ObjectGuid vendorguid;
    uint32 slot;

    recvData >> slot;

    vendorguid[3] = recvData.ReadBit();
    vendorguid[5] = recvData.ReadBit();
    vendorguid[0] = recvData.ReadBit();
    vendorguid[7] = recvData.ReadBit();
    vendorguid[2] = recvData.ReadBit();
    vendorguid[6] = recvData.ReadBit();
    vendorguid[1] = recvData.ReadBit();
    vendorguid[4] = recvData.ReadBit();

    recvData.ReadByteSeq(vendorguid[1]);
    recvData.ReadByteSeq(vendorguid[7]);
    recvData.ReadByteSeq(vendorguid[6]);
    recvData.ReadByteSeq(vendorguid[0]);
    recvData.ReadByteSeq(vendorguid[5]);
    recvData.ReadByteSeq(vendorguid[3]);
    recvData.ReadByteSeq(vendorguid[4]);
    recvData.ReadByteSeq(vendorguid[2]);

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleBuybackItem - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(vendorguid)));
        _player->SendSellError(SELL_ERR_CANT_FIND_VENDOR, NULL, 0);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    Item* pItem = _player->GetItemFromBuyBackSlot(slot);
    if (pItem)
    {
        uint32 price = _player->GetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE + slot - BUYBACK_SLOT_START);
        if (!_player->HasEnoughMoney(uint64(price)))
        {
            _player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, creature, pItem->GetEntry(), 0);
            return;
        }

        ItemPosCountVec dest;
        InventoryResult msg = _player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, pItem, false);
        if (msg == EQUIP_ERR_OK)
        {
            _player->ModifyMoney(-(int32)price);
            _player->RemoveItemFromBuyBackSlot(slot, false);
            _player->ItemAddedQuestCheck(pItem->GetEntry(), pItem->GetCount());
            _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM, pItem->GetEntry(), pItem->GetCount());
            _player->StoreItem(dest, pItem, true);
        }
        else
            _player->SendEquipError(msg, pItem, NULL);
        return;
    }
    else
        _player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, creature, 0, 0);
}

void WorldSession::HandleBuyItemInSlotOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_BUY_ITEM_IN_SLOT");
    uint64 vendorguid, bagguid;
    uint32 item, slot, count;
    uint8 bagslot;

    recvData >> vendorguid >> item  >> slot >> bagguid >> bagslot >> count;

    // client expects count starting at 1, and we send vendorslot+1 to client already
    if (slot > 0)
        --slot;
    else
        return;                                             // cheating

    uint8 bag = NULL_BAG;                                   // init for case invalid bagGUID
    Item* bagItem = NULL;
    // find bag slot by bag guid
    if (bagguid == _player->GetGUID())
        bag = INVENTORY_SLOT_BAG_0;
    else
        bagItem = _player->GetItemByGuid(bagguid);

    if (bagItem && bagItem->IsBag())
        bag = bagItem->GetSlot();

    // bag not found, cheating?
    if (bag == NULL_BAG)
        return;

    GetPlayer()->BuyItemFromVendorSlot(vendorguid, slot, item, count, bag, bagslot);
}

void WorldSession::HandleBuyItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_BUY_ITEM");

    ObjectGuid vendorguid, bagGuid;
    uint32 item, slot, count, bagSlot;
    uint8 itemType; // 1 = item, 2 = currency

    recvData >> bagSlot >> item >> count >> slot;

    bagGuid[2] = recvData.ReadBit();
    vendorguid[0] = recvData.ReadBit();
    bagGuid[5] = recvData.ReadBit();
    vendorguid[7] = recvData.ReadBit();
    bagGuid[0] = recvData.ReadBit();
    itemType = recvData.ReadBits(2);
    bagGuid[6] = recvData.ReadBit();
    bagGuid[4] = recvData.ReadBit();
    vendorguid[2] = recvData.ReadBit();
    vendorguid[1] = recvData.ReadBit();
    bagGuid[3] = recvData.ReadBit();
    vendorguid[5] = recvData.ReadBit();
    bagGuid[7] = recvData.ReadBit();
    vendorguid[4] = recvData.ReadBit();
    bagGuid[1] = recvData.ReadBit();
    vendorguid[3] = recvData.ReadBit();
    vendorguid[6] = recvData.ReadBit();
    recvData.FlushBits();

    recvData.ReadByteSeq(bagGuid[1]);
    recvData.ReadByteSeq(bagGuid[3]);
    recvData.ReadByteSeq(vendorguid[2]);
    recvData.ReadByteSeq(vendorguid[0]);
    recvData.ReadByteSeq(bagGuid[2]);
    recvData.ReadByteSeq(vendorguid[4]);
    recvData.ReadByteSeq(vendorguid[3]);
    recvData.ReadByteSeq(vendorguid[1]);
    recvData.ReadByteSeq(bagGuid[6]);
    recvData.ReadByteSeq(vendorguid[6]);
    recvData.ReadByteSeq(vendorguid[5]);
    recvData.ReadByteSeq(bagGuid[5]);
    recvData.ReadByteSeq(bagGuid[7]);
    recvData.ReadByteSeq(bagGuid[4]);
    recvData.ReadByteSeq(bagGuid[0]);
    recvData.ReadByteSeq(vendorguid[7]);

    // client expects count starting at 1, and we send vendorslot+1 to client already
    if (slot > 0)
        --slot;
    else
        return; // cheating

    if (itemType == ITEM_VENDOR_TYPE_ITEM)
    {
        Item* bagItem = _player->GetItemByGuid(bagGuid);

        uint8 bag = NULL_BAG;
        if (bagItem && bagItem->IsBag())
            bag = bagItem->GetSlot();
        else if (bagGuid == GetPlayer()->GetGUID()) // The client sends the player guid when trying to store an item in the default backpack
            bag = INVENTORY_SLOT_BAG_0;

        GetPlayer()->BuyItemFromVendorSlot(vendorguid, slot, item, count, bag, bagSlot);
    }
    else if (itemType == ITEM_VENDOR_TYPE_CURRENCY)
        GetPlayer()->BuyCurrencyFromVendorSlot(vendorguid, slot, item, count);
    else
        TC_LOG_DEBUG("network", "WORLD: received wrong itemType (%u) in HandleBuyItemOpcode", itemType);
}

void WorldSession::HandleSetCurrencyFlags(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SET_CURRENCY_FLAGS");

    uint32 flags, currencyid;

    recvData >> uint32(flags);
    recvData >> uint32(currencyid);
}

void WorldSession::HandleListInventoryOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;

    guid[1] = recvData.ReadBit();
    guid[0] = recvData.ReadBit();
    guid[6] = recvData.ReadBit();
    guid[3] = recvData.ReadBit();
    guid[5] = recvData.ReadBit();
    guid[4] = recvData.ReadBit();
    guid[7] = recvData.ReadBit();
    guid[2] = recvData.ReadBit();

    recvData.ReadByteSeq(guid[0]);
    recvData.ReadByteSeq(guid[5]);
    recvData.ReadByteSeq(guid[6]);
    recvData.ReadByteSeq(guid[7]);
    recvData.ReadByteSeq(guid[1]);
    recvData.ReadByteSeq(guid[3]);
    recvData.ReadByteSeq(guid[4]);
    recvData.ReadByteSeq(guid[2]);

    if (!GetPlayer()->IsAlive())
        return;

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_LIST_INVENTORY");

    SendListInventory(guid);
}

void WorldSession::SendListInventory(uint64 vendorGuid)
{
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_LIST_INVENTORY");

    Creature* vendor = GetPlayer()->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR);
    if (!vendor)
    {
        TC_LOG_DEBUG("network", "WORLD: SendListInventory - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(vendorGuid)));
        _player->SendSellError(SELL_ERR_CANT_FIND_VENDOR, NULL, 0);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // Stop the npc if moving
    if (vendor->HasUnitState(UNIT_STATE_MOVING))
        vendor->StopMoving();

    VendorItemData const* vendorItems = vendor->GetVendorItems();
    uint32 rawItemCount = vendorItems ? vendorItems->GetItemCount() : 0;

    if (rawItemCount > MAX_VENDOR_ITEMS)
        rawItemCount = MAX_VENDOR_ITEMS; // !Keep in mind client cap is 300 but uint8 max value is 255.

    ByteBuffer itemsData(40 * rawItemCount); // 10 * 4.
    bool hasExtendedCost[MAX_VENDOR_ITEMS];

    const float discountMod = _player->GetReputationPriceDiscount(vendor);
    uint8 count = 0;

    for (uint32 slot = 0; slot < rawItemCount; ++slot)
    {
        VendorItem const* vendorItem = vendorItems->GetItem(slot);
        if (!vendorItem)
            continue;

        if (vendorItem->Type == ITEM_VENDOR_TYPE_ITEM)
        {
            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(vendorItem->item);
            if (!itemTemplate)
                continue;

            uint32 leftInStock = !vendorItem->maxcount ? 0xFFFFFFFF : vendor->GetVendorItemCurrentCount(vendorItem);
            if (!_player->IsGameMaster()) // ignore conditions if GM on
            {
                // Respect allowed class
                if (!(itemTemplate->AllowableClass & _player->getClassMask()) && itemTemplate->Bonding == BIND_WHEN_PICKED_UP)
                    continue;

                // Only display items in vendor lists for the team the player is on
                if ((itemTemplate->Flags2 & ITEM_FLAGS_EXTRA_HORDE_ONLY && _player->GetTeam() == ALLIANCE) ||
                    (itemTemplate->Flags2 & ITEM_FLAGS_EXTRA_ALLIANCE_ONLY && _player->GetTeam() == HORDE))
                    continue;

                // Items sold out are not displayed in list
                if (leftInStock == 0)
                    continue;
            }

            ConditionList conditions = sConditionMgr->GetConditionsForNpcVendorEvent(vendor->GetEntry(), vendorItem->item);
            if (!sConditionMgr->IsObjectMeetToConditions(_player, vendor, conditions))
            {
                TC_LOG_DEBUG("condition", "SendListInventory: conditions not met for creature entry %u item %u", vendor->GetEntry(), vendorItem->item);
                continue;
            }

            int32 price = vendorItem->IsGoldRequired(itemTemplate) ? uint32(floor(itemTemplate->BuyPrice * discountMod)) : 0;

            if (int32 priceMod = _player->GetTotalAuraModifier(SPELL_AURA_MOD_VENDOR_ITEMS_PRICES))
                price -= CalculatePct(price, priceMod);

            itemsData << int32(-1);
            itemsData << uint32(vendorItem->Type);                  // 1 is items, 2 is currency
            itemsData << uint32(itemTemplate->BuyCount);

            if (vendorItem->ExtendedCost)
            {
                hasExtendedCost[slot] = true;
                itemsData << uint32(vendorItem->ExtendedCost);
            }
            else
                hasExtendedCost[slot] = false;

            itemsData << uint32(price);
            itemsData << uint32(vendorItem->item);
            itemsData << uint32(slot + 1);                          // client expects counting to start at 1
            itemsData << int32(leftInStock);
            itemsData << uint32(0);
            itemsData << uint32(itemTemplate->DisplayInfoID);

            if (count++ >= MAX_VENDOR_ITEMS)
                break;

            // Increase count.
            count++;
        }

        else if (vendorItem->Type == ITEM_VENDOR_TYPE_CURRENCY)
        {
            CurrencyTypesEntry const* currencyTemplate = sCurrencyTypesStore.LookupEntry(vendorItem->item);
            if (!currencyTemplate)
                continue;

            if (!vendorItem->ExtendedCost)
                continue; // there's no price defined for currencies, only extendedcost is used

            itemsData << int32(-1);
            itemsData << uint32(vendorItem->Type);                  // 1 is items, 2 is currency
            itemsData << uint32(0);                                 // buy count

            hasExtendedCost[slot] = true;
            itemsData << uint32(vendorItem->ExtendedCost);

            itemsData << uint32(0);                                 // price, only seen currency types that have Extended cost
            itemsData << uint32(vendorItem->item);
            itemsData << uint32(slot + 1);                          // client expects counting to start at 1
            itemsData << int32(-1);                                 // left in stock
            itemsData << uint32(0);
            itemsData << uint32(0);                                 // displayId

            if (count++ >= MAX_VENDOR_ITEMS)
                break;

            // Increase count.
            count++;
        }

        else // Else error, neither item or currency.
        {
            TC_LOG_ERROR("network", "SendListInventory - Vendor creature entry %u, item entry %u type is neither Item or Currency !", vendor->GetEntry(), vendorItem->item);
            continue;
        }
    }

    ObjectGuid guid = vendorGuid;

    WorldPacket data(SMSG_LIST_INVENTORY, 12 + itemsData.size());
    data.WriteBit(guid[4]);
    data.WriteBits(count, 18);                                      // item count

    for (uint32 i = 0; i < count; i++)
    {
        data.WriteBit(0);                                           // unknown
        data.WriteBit(1);                                           // has unknown
        data.WriteBit(!hasExtendedCost[i]);                         // has extended cost
    }

    data.WriteBit(guid[1]);
    data.WriteBit(guid[6]);
    data.WriteBit(guid[2]);
    data.WriteBit(guid[5]);
    data.WriteBit(guid[7]);
    data.WriteBit(guid[0]);
    data.WriteBit(guid[3]);
    data.FlushBits();

    data.WriteByteSeq(guid[3]);
    data.append(itemsData);
    data.WriteByteSeq(guid[6]);
    data.WriteByteSeq(guid[0]);
    data.WriteByteSeq(guid[2]);
    data.WriteByteSeq(guid[5]);

    /* It doesn't matter what value is used here (PROBABLY its full vendor size)
     * What matters is that if count of items we can see is 0 and this field is 1
     * then client will open the vendor list, otherwise it won't
     */
    if (rawItemCount)
        data << uint8(rawItemCount);
    else
        data << uint8(vendor->IsArmorer());

    data.WriteByteSeq(guid[1]);
    data.WriteByteSeq(guid[4]);
    data.WriteByteSeq(guid[7]);

    SendPacket(&data);
}

void WorldSession::HandleAutoStoreBagItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUTOSTORE_BAG_ITEM");

    uint8 srcBag, srcSlot, dstBag;

    recvData >> srcBag >> dstBag >> srcSlot;

    // TC_LOG_DEBUG("network", "STORAGE: Received Item auto-store in bag: source bag = %u, source slot = %u, dest bag = %u", srcBag, srcSlot, dstBag);

    Item* pItem = _player->GetItemByPos(srcBag, srcSlot);
    if (!pItem)
        return;

    if (!_player->IsValidPos(dstBag, NULL_SLOT, false))      // Can be autostore pos.
    {
        _player->SendEquipError(EQUIP_ERR_WRONG_SLOT, NULL, NULL);
        return;
    }

    uint16 src = pItem->GetPos();

    // Check unequip potability for equipped items and bank bags.
    if (_player->IsEquipmentPos(src) || _player->IsBagPos(src))
    {
        InventoryResult msg = _player->CanUnequipItem(src, !_player->IsBagPos (src));
        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, pItem, NULL);
            return;
        }
    }

    ItemPosCountVec dest;
    InventoryResult msg = _player->CanStoreItem(dstBag, NULL_SLOT, dest, pItem, false);
    if (msg != EQUIP_ERR_OK)
    {
        _player->SendEquipError(msg, pItem, NULL);
        return;
    }

    // no-op: placed in same slot
    if (dest.size() == 1 && dest[0].pos == src)
    {
        // just remove grey item state
        _player->SendEquipError(EQUIP_ERR_INTERNAL_BAG_ERROR, pItem, NULL);
        return;
    }

    _player->RemoveItem(srcBag, srcSlot, true);
    _player->StoreItem(dest, pItem, true);
}

void WorldSession::HandleBuyBankSlotOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_BUY_BANK_SLOT");

    ObjectGuid guid;

    guid[7] = recvData.ReadBit();
    guid[2] = recvData.ReadBit();
    guid[3] = recvData.ReadBit();
    guid[1] = recvData.ReadBit();
    guid[5] = recvData.ReadBit();
    guid[4] = recvData.ReadBit();
    guid[0] = recvData.ReadBit();
    guid[6] = recvData.ReadBit();

    recvData.ReadByteSeq(guid[7]);
    recvData.ReadByteSeq(guid[0]);
    recvData.ReadByteSeq(guid[3]);
    recvData.ReadByteSeq(guid[1]);
    recvData.ReadByteSeq(guid[6]);
    recvData.ReadByteSeq(guid[2]);
    recvData.ReadByteSeq(guid[4]);
    recvData.ReadByteSeq(guid[5]);


    // cheating protection
    /* not critical if "cheated", and check skip allow by slots in bank windows open by .bank command.
    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_BANKER);
    if (!creature)
    {
        TC_LOG_DEBUG("WORLD: HandleBuyBankSlotOpcode - Unit (GUID: %u) not found or you can't interact with him.", uint32(GUID_LOPART(guid)));
        return;
    }
    */

    uint32 slot = _player->GetBankBagSlotCount();

    // next slot
    ++slot;

    TC_LOG_INFO("network", "PLAYER: Buy bank bag slot, slot number = %u", slot);

    BankBagSlotPricesEntry const* slotEntry = sBankBagSlotPricesStore.LookupEntry(slot);

    WorldPacket data(SMSG_BUY_BANK_SLOT_RESULT, 4);

    if (!slotEntry)
    {
        data << uint32(ERR_BANKSLOT_FAILED_TOO_MANY);
        SendPacket(&data);
        return;
    }

    uint32 price = slotEntry->price;

    if (!_player->HasEnoughMoney(uint64(price)))
    {
        data << uint32(ERR_BANKSLOT_INSUFFICIENT_FUNDS);
        SendPacket(&data);
        return;
    }

    _player->SetBankBagSlotCount(slot);
    _player->ModifyMoney(-int64(price));

     data << uint32(ERR_BANKSLOT_OK);
     SendPacket(&data);

    _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT);
}

void WorldSession::HandleAutoBankItemOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUTOBANK_ITEM");

    uint8 srcBag, srcSlot;

    recvPacket >> srcSlot >> srcBag;
    recvPacket.rfinish();

    // TC_LOG_DEBUG("network", "STORAGE: Received Item auto-bank: source bag = %u, source slot = %u", srcBag, srcSlot);

    Item* pItem = _player->GetItemByPos(srcBag, srcSlot);
    if (!pItem)
        return;

    ItemPosCountVec dest;
    InventoryResult msg = _player->CanBankItem(NULL_BAG, NULL_SLOT, dest, pItem, false);
    if (msg != EQUIP_ERR_OK)
    {
        _player->SendEquipError(msg, pItem, NULL);
        return;
    }

    if (dest.size() == 1 && dest[0].pos == pItem->GetPos())
    {
        _player->SendEquipError(EQUIP_ERR_CANT_SWAP, pItem, NULL);
        return;
    }

    _player->RemoveItem(srcBag, srcSlot, true);
    _player->ItemRemovedQuestCheck(pItem->GetEntry(), pItem->GetCount());
    _player->BankItem(dest, pItem, true);
}

void WorldSession::HandleAutoStoreBankItemOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUTOSTORE_BANK_ITEM");

    uint8 srcBag, srcSlot;

    recvPacket >> srcBag >> srcSlot;

    // TC_LOG_DEBUG("network", "STORAGE: Received Item auto-store in bank: source bag = %u, source slot = %u", srcBag, srcSlot);

    Item* pItem = _player->GetItemByPos(srcBag, srcSlot);
    if (!pItem)
        return;

    if (_player->IsBankPos(srcBag, srcSlot))                 // moving from bank to inventory
    {
        ItemPosCountVec dest;
        InventoryResult msg = _player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, pItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, pItem, NULL);
            return;
        }

        _player->RemoveItem(srcBag, srcSlot, true);
        if (Item const* storedItem = _player->StoreItem(dest, pItem, true))
            _player->ItemAddedQuestCheck(storedItem->GetEntry(), storedItem->GetCount());
    }
    else                                                    // moving from inventory to bank
    {
        ItemPosCountVec dest;
        InventoryResult msg = _player->CanBankItem(NULL_BAG, NULL_SLOT, dest, pItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, pItem, NULL);
            return;
        }

        _player->RemoveItem(srcBag, srcSlot, true);
        _player->BankItem(dest, pItem, true);
    }
}

void WorldSession::SendEnchantmentLog(uint64 target, uint64 caster, uint32 itemId, uint32 enchantId)
{
    WorldPacket data(SMSG_ENCHANTMENTLOG, (8+8+4+4));
    data.appendPackGUID(target);
    data.appendPackGUID(caster);
    data << uint32(itemId);
    data << uint32(enchantId);
    GetPlayer()->SendMessageToSet(&data, true);
}

void WorldSession::SendItemEnchantTimeUpdate(uint64 Playerguid, uint64 Itemguid, uint32 slot, uint32 Duration)
{
                                                            // last check 2.0.10
    WorldPacket data(SMSG_ITEM_ENCHANT_TIME_UPDATE, (8+4+4+8));
    data << uint64(Itemguid);
    data << uint32(slot);
    data << uint32(Duration);
    data << uint64(Playerguid);
    SendPacket(&data);
}

void WorldSession::HandleWrapItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received CMSG_WRAP_ITEM");

    uint8 gift_bag, gift_slot, item_bag, item_slot;

    recvData >> gift_bag >> gift_slot;                     // paper
    recvData >> item_bag >> item_slot;                     // item

    TC_LOG_DEBUG("network", "WRAP: receive gift_bag = %u, gift_slot = %u, item_bag = %u, item_slot = %u", gift_bag, gift_slot, item_bag, item_slot);

    Item* gift = _player->GetItemByPos(gift_bag, gift_slot);
    if (!gift)
    {
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, gift, NULL);
        return;
    }

    if (!(gift->GetTemplate()->Flags & ITEM_PROTO_FLAG_WRAPPER)) // cheating: non-wrapper wrapper
    {
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, gift, NULL);
        return;
    }

    Item* item = _player->GetItemByPos(item_bag, item_slot);

    if (!item)
    {
        _player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, item, NULL);
        return;
    }

    if (item == gift)                                          // not possable with pacjket from real client
    {
        _player->SendEquipError(EQUIP_ERR_CANT_WRAP_WRAPPED, item, NULL);
        return;
    }

    if (item->IsEquipped())
    {
        _player->SendEquipError(EQUIP_ERR_CANT_WRAP_EQUIPPED, item, NULL);
        return;
    }

    if (item->GetUInt64Value(ITEM_FIELD_GIFT_CREATOR))        // HasFlag(ITEM_FIELD_DYNAMIC_FLAGS, ITEM_FLAGS_WRAPPED);
    {
        _player->SendEquipError(EQUIP_ERR_CANT_WRAP_WRAPPED, item, NULL);
        return;
    }

    if (item->IsBag())
    {
        _player->SendEquipError(EQUIP_ERR_CANT_WRAP_BAGS, item, NULL);
        return;
    }

    if (item->IsSoulBound())
    {
        _player->SendEquipError(EQUIP_ERR_CANT_WRAP_BOUND, item, NULL);
        return;
    }

    if (item->GetMaxStackCount() != 1)
    {
        _player->SendEquipError(EQUIP_ERR_CANT_WRAP_STACKABLE, item, NULL);
        return;
    }

    // maybe not correct check  (it is better than nothing)
    if (item->GetTemplate()->MaxCount > 0)
    {
        _player->SendEquipError(EQUIP_ERR_CANT_WRAP_UNIQUE, item, NULL);
        return;
    }

    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_GIFT);
    stmt->setUInt32(0, GUID_LOPART(item->GetOwnerGUID()));
    stmt->setUInt32(1, item->GetGUIDLow());
    stmt->setUInt32(2, item->GetEntry());
    stmt->setUInt32(3, item->GetUInt32Value(ITEM_FIELD_DYNAMIC_FLAGS));
    trans->Append(stmt);

    item->SetEntry(gift->GetEntry());

    switch (item->GetEntry())
    {
        case 5042:  item->SetEntry(5043); break;
        case 5048:  item->SetEntry(5044); break;
        case 17303: item->SetEntry(17302); break;
        case 17304: item->SetEntry(17305); break;
        case 17307: item->SetEntry(17308); break;
        case 21830: item->SetEntry(21831); break;
    }
    item->SetUInt64Value(ITEM_FIELD_GIFT_CREATOR, _player->GetGUID());
    item->SetUInt32Value(ITEM_FIELD_DYNAMIC_FLAGS, ITEM_FLAG_WRAPPED);
    item->SetState(ITEM_CHANGED, _player);

    if (item->GetState() == ITEM_NEW)                          // save new item, to have alway for `character_gifts` record in `item_instance`
    {
        // after save it will be impossible to remove the item from the queue
        item->RemoveFromUpdateQueueOf(_player);
        item->SaveToDB(trans);                                   // item gave inventory record unchanged and can be save standalone
    }
    CharacterDatabase.CommitTransaction(trans);

    uint32 count = 1;
    _player->DestroyItemCount(gift, count, true);
}

void WorldSession::HandleSocketOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SOCKET_GEMS");

    uint64 item_guid;
    uint64 gem_guids[MAX_GEM_SOCKETS];

    recvData >> item_guid;
    if (!item_guid)
        return;

    for (int i = 0; i < MAX_GEM_SOCKETS; ++i)
        recvData >> gem_guids[i];

    //cheat -> tried to socket same gem multiple times
    if ((gem_guids[0] && (gem_guids[0] == gem_guids[1] || gem_guids[0] == gem_guids[2])) ||
        (gem_guids[1] && (gem_guids[1] == gem_guids[2])))
        return;

    Item* itemTarget = _player->GetItemByGuid(item_guid);
    if (!itemTarget)                                         //missing item to socket
        return;

    ItemTemplate const* itemProto = itemTarget->GetTemplate();
    if (!itemProto)
        return;

    //this slot is excepted when applying / removing meta gem bonus
    uint8 slot = itemTarget->IsEquipped() ? itemTarget->GetSlot() : uint8(NULL_SLOT);

    Item* Gems[MAX_GEM_SOCKETS];
    for (int i = 0; i < MAX_GEM_SOCKETS; ++i)
        Gems[i] = gem_guids[i] ? _player->GetItemByGuid(gem_guids[i]) : NULL;

    GemPropertiesEntry const* GemProps[MAX_GEM_SOCKETS];
    for (int i = 0; i < MAX_GEM_SOCKETS; ++i)                //get geminfo from dbc storage
        GemProps[i] = (Gems[i]) ? sGemPropertiesStore.LookupEntry(Gems[i]->GetTemplate()->GemProperties) : NULL;

    // Find first prismatic socket
    int32 firstPrismatic = 0;
    while (firstPrismatic < MAX_GEM_SOCKETS && itemProto->Socket[firstPrismatic].Color)
        ++firstPrismatic;

    for (int i = 0; i < MAX_GEM_SOCKETS; ++i)                //check for hack maybe
    {
        if (!GemProps[i])
            continue;

        // tried to put gem in socket where no socket exists (take care about prismatic sockets)
        if (!itemProto->Socket[i].Color)
        {
            // no prismatic socket
            if (!itemTarget->GetEnchantmentId(PRISMATIC_ENCHANTMENT_SLOT))
                return;

            if (i != firstPrismatic)
                return;
        }

        // tried to put normal gem in meta socket
        if (itemProto->Socket[i].Color == SOCKET_COLOR_META && GemProps[i]->color != SOCKET_COLOR_META)
            return;

        // tried to put meta gem in normal socket
        if (itemProto->Socket[i].Color != SOCKET_COLOR_META && GemProps[i]->color == SOCKET_COLOR_META)
            return;

        // tried to put normal gem in cogwheel socket
        if (itemProto->Socket[i].Color == SOCKET_COLOR_COGWHEEL && GemProps[i]->color != SOCKET_COLOR_COGWHEEL)
            return;

        // tried to put cogwheel gem in normal socket
        if (itemProto->Socket[i].Color != SOCKET_COLOR_COGWHEEL && GemProps[i]->color == SOCKET_COLOR_COGWHEEL)
            return;
    }

    uint32 GemEnchants[MAX_GEM_SOCKETS];
    uint32 OldEnchants[MAX_GEM_SOCKETS];
    for (int i = 0; i < MAX_GEM_SOCKETS; ++i)                //get new and old enchantments
    {
        GemEnchants[i] = (GemProps[i]) ? GemProps[i]->spellitemenchantement : 0;
        OldEnchants[i] = itemTarget->GetEnchantmentId(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT+i));
    }

    // check unique-equipped conditions
    for (int i = 0; i < MAX_GEM_SOCKETS; ++i)
    {
        if (!Gems[i])
            continue;

        // continue check for case when attempt add 2 similar unique equipped gems in one item.
        ItemTemplate const* iGemProto = Gems[i]->GetTemplate();

        // unique item (for new and already placed bit removed enchantments
        if (iGemProto->Flags & ITEM_PROTO_FLAG_UNIQUE_EQUIPPED)
        {
            for (int j = 0; j < MAX_GEM_SOCKETS; ++j)
            {
                if (i == j)                                    // skip self
                    continue;

                if (Gems[j])
                {
                    if (iGemProto->ItemId == Gems[j]->GetEntry())
                    {
                        _player->SendEquipError(EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE_SOCKETED, itemTarget, NULL);
                        return;
                    }
                }
                else if (OldEnchants[j])
                {
                    if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(OldEnchants[j]))
                    {
                        if (iGemProto->ItemId == enchantEntry->GemID)
                        {
                            _player->SendEquipError(EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE_SOCKETED, itemTarget, NULL);
                            return;
                        }
                    }
                }
            }
        }

        // unique limit type item
        int32 limit_newcount = 0;
        if (iGemProto->ItemLimitCategory)
        {
            if (ItemLimitCategoryEntry const* limitEntry = sItemLimitCategoryStore.LookupEntry(iGemProto->ItemLimitCategory))
            {
                // NOTE: limitEntry->mode is not checked because if item has limit then it is applied in equip case
                for (int j = 0; j < MAX_GEM_SOCKETS; ++j)
                {
                    if (Gems[j])
                    {
                        // new gem
                        if (iGemProto->ItemLimitCategory == Gems[j]->GetTemplate()->ItemLimitCategory)
                            ++limit_newcount;
                    }
                    else if (OldEnchants[j])
                    {
                        // existing gem
                        if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(OldEnchants[j]))
                            if (ItemTemplate const* jProto = sObjectMgr->GetItemTemplate(enchantEntry->GemID))
                                if (iGemProto->ItemLimitCategory == jProto->ItemLimitCategory)
                                    ++limit_newcount;
                    }
                }

                if (limit_newcount > 0 && uint32(limit_newcount) > limitEntry->maxCount)
                {
                    _player->SendEquipError(EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE_SOCKETED, itemTarget, NULL);
                    return;
                }
            }
        }

        // for equipped item check all equipment for duplicate equipped gems
        if (itemTarget->IsEquipped())
        {
            if (InventoryResult res = _player->CanEquipUniqueItem(Gems[i], slot, std::max(limit_newcount, 0)))
            {
                _player->SendEquipError(res, itemTarget, NULL);
                return;
            }
        }
    }

    bool SocketBonusActivated = itemTarget->GemsFitSockets();    //save state of socketbonus
    _player->ToggleMetaGemsActive(slot, false);             //turn off all metagems (except for the target item)

    //if a meta gem is being equipped, all information has to be written to the item before testing if the conditions for the gem are met

    //remove ALL enchants
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++enchant_slot)
        _player->ApplyEnchantment(itemTarget, EnchantmentSlot(enchant_slot), false);

    for (int i = 0; i < MAX_GEM_SOCKETS; ++i)
    {
        if (GemEnchants[i])
        {
            itemTarget->SetEnchantment(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT+i), GemEnchants[i], 0, 0, _player->GetGUID());
            if (Item* guidItem = _player->GetItemByGuid(gem_guids[i]))
            {
                uint32 gemCount = 1;
                _player->DestroyItemCount(guidItem, gemCount, true);
            }
        }
    }

    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT+MAX_GEM_SOCKETS; ++enchant_slot)
        _player->ApplyEnchantment(itemTarget, EnchantmentSlot(enchant_slot), true);

    bool SocketBonusToBeActivated = itemTarget->GemsFitSockets();//current socketbonus state
    if (SocketBonusActivated ^ SocketBonusToBeActivated)     //if there was a change...
    {
        _player->ApplyEnchantment(itemTarget, BONUS_ENCHANTMENT_SLOT, false);
        itemTarget->SetEnchantment(BONUS_ENCHANTMENT_SLOT, (SocketBonusToBeActivated ? itemTarget->GetTemplate()->socketBonus : 0), 0, 0, _player->GetGUID());
        _player->ApplyEnchantment(itemTarget, BONUS_ENCHANTMENT_SLOT, true);
        //it is not displayed, client has an inbuilt system to determine if the bonus is activated
    }

    _player->ToggleMetaGemsActive(slot, true);              //turn on all metagems (except for target item)

    _player->RemoveTradeableItem(itemTarget);
    itemTarget->ClearSoulboundTradeable(_player);           // clear tradeable flag

    itemTarget->SendUpdateSockets();
}

void WorldSession::HandleCancelTempEnchantmentOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_CANCEL_TEMP_ENCHANTMENT");

    uint32 slot;

    recvData >> slot;

    // apply only to equipped item
    if (!Player::IsEquipmentPos(INVENTORY_SLOT_BAG_0, slot))
        return;

    Item* item = GetPlayer()->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

    if (!item)
        return;

    if (!item->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return;

    GetPlayer()->ApplyEnchantment(item, TEMP_ENCHANTMENT_SLOT, false);
    item->ClearEnchantment(TEMP_ENCHANTMENT_SLOT);
}

void WorldSession::HandleItemRefundInfoRequest(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ITEM_REFUND_INFO");

    uint64 guid;
    recvData >> guid;                                      // item guid

    Item* item = _player->GetItemByGuid(guid);
    if (!item)
    {
        TC_LOG_DEBUG("network", "Item refund: item not found!");
        return;
    }

    GetPlayer()->SendRefundInfo(item);
}

void WorldSession::HandleItemRefund(WorldPacket &recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ITEM_REFUND");
    uint64 guid;
    recvData >> guid;                                      // item guid

    Item* item = _player->GetItemByGuid(guid);
    if (!item)
    {
        TC_LOG_DEBUG("network", "Item refund: item not found!");
        return;
    }

    GetPlayer()->RefundItem(item);
}

/**
 * Handles the packet sent by the client when requesting information about item text.
 *
 * This function is called when player clicks on item which has some flag set
 */
void WorldSession::HandleItemTextQuery(WorldPacket& recvData )
{
    TC_LOG_DEBUG("network", "Received CMSG_ITEM_TEXT_QUERY");

    uint64 itemGuid;
    recvData >> itemGuid;

    WorldPacket data(SMSG_ITEM_TEXT_QUERY_RESPONSE, 14);    // guess size

    if (Item* item = _player->GetItemByGuid(itemGuid))
    {
        data << uint8(0);                                       // has text
        data << uint64(itemGuid);                               // item guid
        data << item->GetText();
    }
    else
    {
        data << uint8(1);                                       // no text
    }

    SendPacket(&data);
}

void WorldSession::HandleTransmogrifyItems(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_TRANSMOGRIFY_ITEMS");

    Player* player = GetPlayer();

    // Read the Transmog data.
    uint32 count;
    ObjectGuid npcGuid;

    npcGuid[3] = recvData.ReadBit();
    npcGuid[2] = recvData.ReadBit();
    npcGuid[4] = recvData.ReadBit();
    npcGuid[5] = recvData.ReadBit();
    npcGuid[1] = recvData.ReadBit();
    npcGuid[0] = recvData.ReadBit();

    count = recvData.ReadBits(21);

    npcGuid[7] = recvData.ReadBit();
    npcGuid[6] = recvData.ReadBit();

    if (count < EQUIPMENT_SLOT_START || count >= EQUIPMENT_SLOT_END)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleTransmogrifyItems - Player (GUID: %u, name: %s) sent a wrong count (%u) when transmogrifying items.", player->GetGUIDLow(), player->GetName().c_str(), count);
        recvData.rfinish();
        return;
    }

    std::vector<ObjectGuid> originalItemGuid(count, ObjectGuid(0));
    std::vector<ObjectGuid> targetItemGuid(count, ObjectGuid(0));

    std::vector<uint32> newEntries(count, 0);
    std::vector<uint32> slots(count, 0);

    std::vector<bool> hasItemGuid1(count, false); // Original Item.
    std::vector<bool> hasItemGuid2(count, false); // Item to transmog to.

    for (uint8 i = 0; i < count; ++i)
    {
        hasItemGuid1[i] = recvData.ReadBit();
        hasItemGuid2[i] = recvData.ReadBit();

        if (hasItemGuid1[i])
        {
            originalItemGuid[i][5] = recvData.ReadBit();
            originalItemGuid[i][6] = recvData.ReadBit();
            originalItemGuid[i][4] = recvData.ReadBit();
            originalItemGuid[i][0] = recvData.ReadBit();
            originalItemGuid[i][7] = recvData.ReadBit();
            originalItemGuid[i][3] = recvData.ReadBit();
            originalItemGuid[i][1] = recvData.ReadBit();
            originalItemGuid[i][2] = recvData.ReadBit();
        }

        if (hasItemGuid2[i])
        {
            targetItemGuid[i][3] = recvData.ReadBit();
            targetItemGuid[i][6] = recvData.ReadBit();
            targetItemGuid[i][4] = recvData.ReadBit();
            targetItemGuid[i][0] = recvData.ReadBit();
            targetItemGuid[i][1] = recvData.ReadBit();
            targetItemGuid[i][7] = recvData.ReadBit();
            targetItemGuid[i][5] = recvData.ReadBit();
            targetItemGuid[i][2] = recvData.ReadBit();
        }
    }

    recvData.FlushBits();

    for (uint8 i = 0; i < count; ++i)
    {
        recvData >> newEntries[i];
        recvData >> slots[i];
    }

    recvData.ReadByteSeq(npcGuid[5]);
    recvData.ReadByteSeq(npcGuid[4]);
    recvData.ReadByteSeq(npcGuid[1]);
    recvData.ReadByteSeq(npcGuid[0]);
    recvData.ReadByteSeq(npcGuid[2]);
    recvData.ReadByteSeq(npcGuid[7]);
    recvData.ReadByteSeq(npcGuid[6]);
    recvData.ReadByteSeq(npcGuid[3]);

    for (uint8 i = 0; i < count; ++i)
    {
        if (hasItemGuid2[i])
        {
            recvData.ReadByteSeq(targetItemGuid[i][4]);
            recvData.ReadByteSeq(targetItemGuid[i][0]);
            recvData.ReadByteSeq(targetItemGuid[i][5]);
            recvData.ReadByteSeq(targetItemGuid[i][6]);
            recvData.ReadByteSeq(targetItemGuid[i][2]);
            recvData.ReadByteSeq(targetItemGuid[i][7]);
            recvData.ReadByteSeq(targetItemGuid[i][1]);
            recvData.ReadByteSeq(targetItemGuid[i][3]);
        }

        if (hasItemGuid1[i])
        {
            recvData.ReadByteSeq(originalItemGuid[i][3]);
            recvData.ReadByteSeq(originalItemGuid[i][6]);
            recvData.ReadByteSeq(originalItemGuid[i][2]);
            recvData.ReadByteSeq(originalItemGuid[i][7]);
            recvData.ReadByteSeq(originalItemGuid[i][4]);
            recvData.ReadByteSeq(originalItemGuid[i][5]);
            recvData.ReadByteSeq(originalItemGuid[i][0]);
            recvData.ReadByteSeq(originalItemGuid[i][1]);
        }
    }

    // Validate.
    if (!player->GetNPCIfCanInteractWith(npcGuid, UNIT_NPC_FLAG_TRANSMOGRIFIER))
    {
        TC_LOG_DEBUG("network", "WORLD: HandleTransmogrifyItems - Unit (GUID: %u) not found or player can't interact with it.", GUID_LOPART(npcGuid));
        return;
    }

    int32 cost = 0;
    for (uint8 i = 0; i < count; ++i)
    {
        // Slot of the transmogrified item.
        if (slots[i] < EQUIPMENT_SLOT_START || slots[i] >= EQUIPMENT_SLOT_END)
        {
            TC_LOG_DEBUG("network", "WORLD: HandleTransmogrifyItems - Player (GUID: %u, name: %s) tried to transmogrify an item (lowguid: %u) with a wrong slot (%u) when transmogrifying items.", player->GetGUIDLow(), player->GetName().c_str(), GUID_LOPART(originalItemGuid[i]), slots[i]);
            return;
        }

        // Entry of the transmogrifier item, if it's not 0.
        if (newEntries[i])
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(newEntries[i]);
            if (!proto)
            {
                TC_LOG_DEBUG("network", "WORLD: HandleTransmogrifyItems - Player (GUID: %u, name: %s) tried to transmogrify to an invalid item (entry: %u).", player->GetGUIDLow(), player->GetName().c_str(), newEntries[i]);
                return;
            }

            if (!player->HasItemCount(newEntries[i], 1, false))
                return;
        }

        Item* itemTransmogrifier = NULL;

        // Guid of the transmogrifier item, if it's not 0.
        if (targetItemGuid[i])
        {
            itemTransmogrifier = player->GetItemByGuid(targetItemGuid[i]);
            if (!itemTransmogrifier)
            {
                TC_LOG_DEBUG("network", "WORLD: HandleTransmogrifyItems - Player (GUID: %u, name: %s) tried to transmogrify with an invalid item (lowguid: %u).", player->GetGUIDLow(), player->GetName().c_str(), GUID_LOPART(targetItemGuid[i]));
                return;
            }
        }

        // Transmogrified item.
        Item* itemTransmogrified = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slots[i]);
        if (!itemTransmogrified)
        {
            TC_LOG_DEBUG("network", "WORLD: HandleTransmogrifyItems - Player (GUID: %u, name: %s) tried to transmogrify an invalid item in a valid slot (slot: %u).", player->GetGUIDLow(), player->GetName().c_str(), slots[i]);
            return;
        }

        if (!newEntries[i]) // Reset look.
        {
            if (itemTransmogrified->GetTransmogrifyId() != 0) // If the item has a transmog id.
            {
                itemTransmogrified->SetDynamicUInt32Value(ITEM_DYNAMIC_MODIFIERS, 1, 0);
                itemTransmogrified->RemoveFlag(ITEM_FIELD_MODIFIERS_MASK, 0x3);
            }

            itemTransmogrified->SetState(ITEM_CHANGED, player);
            player->SetVisibleItemSlot(slots[i], itemTransmogrified);
        }
        else
        {
            if (!Item::CanTransmogrifyItemWithItem(itemTransmogrified, itemTransmogrifier))
            {
                TC_LOG_DEBUG("network", "WORLD: HandleTransmogrifyItems - Player (GUID: %u, name: %s) failed CanTransmogrifyItemWithItem (%u with %u).", player->GetGUIDLow(), player->GetName().c_str(), itemTransmogrified->GetEntry(), itemTransmogrifier->GetEntry());
                return;
            }

            // All okay, proceed.
            itemTransmogrified->SetDynamicUInt32Value(ITEM_DYNAMIC_MODIFIERS, 1, newEntries[i]);
            itemTransmogrified->SetFlag(ITEM_FIELD_MODIFIERS_MASK, 0x3);
            player->SetVisibleItemSlot(slots[i], itemTransmogrified);

            itemTransmogrified->UpdatePlayedTime(player);

            itemTransmogrified->SetOwnerGUID(player->GetGUID());
            itemTransmogrified->SetNotRefundable(player);
            itemTransmogrified->ClearSoulboundTradeable(player);

            if (itemTransmogrifier->GetTemplate()->Bonding == BIND_WHEN_EQUIPED || itemTransmogrifier->GetTemplate()->Bonding == BIND_WHEN_USE)
                itemTransmogrifier->SetBinding(true);

            itemTransmogrifier->SetOwnerGUID(player->GetGUID());
            itemTransmogrifier->SetNotRefundable(player);
            itemTransmogrifier->ClearSoulboundTradeable(player);

            itemTransmogrified->SetState(ITEM_CHANGED, player);

            cost += itemTransmogrified->GetSpecialPrice();
        }
    }

    // Trusting the client, if it got here it has to have enough money... unless client was modified.
    if (cost) // 0 cost if reverting look
        player->ModifyMoney(-cost);
}

void WorldSession::SendReforgeResult(bool success)
{
    WorldPacket data(SMSG_REFORGE_RESULT, 1);
    data.WriteBit(success);
    data.FlushBits();

    SendPacket(&data);
}

void WorldSession::HandleReforgeItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_REFORGE_ITEM");

    uint32 slot, reforgeEntry;
    ObjectGuid guid;
    uint32 bag;
    Player* player = GetPlayer();

    recvData >> slot >> reforgeEntry >> bag;

    guid[3] = recvData.ReadBit();
    guid[5] = recvData.ReadBit();
    guid[4] = recvData.ReadBit();
    guid[6] = recvData.ReadBit();
    guid[1] = recvData.ReadBit();
    guid[0] = recvData.ReadBit();
    guid[7] = recvData.ReadBit();
    guid[2] = recvData.ReadBit();

    recvData.ReadByteSeq(guid[2]);
    recvData.ReadByteSeq(guid[0]);
    recvData.ReadByteSeq(guid[6]);
    recvData.ReadByteSeq(guid[4]);
    recvData.ReadByteSeq(guid[3]);
    recvData.ReadByteSeq(guid[5]);
    recvData.ReadByteSeq(guid[1]);
    recvData.ReadByteSeq(guid[7]);

    if (!player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_REFORGER))
    {
        TC_LOG_DEBUG("network", "WORLD: HandleReforgeItemOpcode - Unit (GUID: %u) not found or player can't interact with it.", GUID_LOPART(guid));
        SendReforgeResult(false);
        return;
    }

    Item* item = player->GetItemByPos(bag, slot);

    if (!item)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleReforgeItemOpcode - Player (Guid: %u Name: %s) tried to reforge an invalid/non-existant item.", player->GetGUIDLow(), player->GetName().c_str());
        SendReforgeResult(false);
        return;
    }

    if (!reforgeEntry)
    {
        // Reset the item.
        if (item->IsEquipped() && !item->IsBroken())
            player->ApplyReforgeEnchantment(item, false);

        if (item->GetReforgeId() != 0) // If the item has a reforge id.
        {
            item->SetDynamicUInt32Value(ITEM_DYNAMIC_MODIFIERS, 0, 0);
            item->RemoveFlag(ITEM_FIELD_MODIFIERS_MASK, 0x1);
        }

        item->SetState(ITEM_CHANGED, player);
        SendReforgeResult(true);
        return;
    }

    ItemReforgeEntry const* stats = sItemReforgeStore.LookupEntry(reforgeEntry);
    if (!stats)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleReforgeItemOpcode - Player (Guid: %u Name: %s) tried to reforge an item with invalid reforge entry (%u).", player->GetGUIDLow(), player->GetName().c_str(), reforgeEntry);
        SendReforgeResult(false);
        return;
    }

    if (!item->GetReforgableStat(ItemModType(stats->SourceStat)) || item->GetReforgableStat(ItemModType(stats->FinalStat))) // Cheating, you cant reforge to a stat that the item already has, nor reforge from a stat that the item does not have
    {
        SendReforgeResult(false);
        return;
    }

    if (!player->HasEnoughMoney(uint64(item->GetSpecialPrice()))) // Cheating!
    {
        SendReforgeResult(false);
        return;
    }

    if (item->GetReforgeId() != 0)
    {
        SendReforgeResult(false);
        return;
    }

    player->ModifyMoney(-int64(item->GetSpecialPrice()));

    item->SetDynamicUInt32Value(ITEM_DYNAMIC_MODIFIERS, 0, reforgeEntry);
    item->SetFlag(ITEM_FIELD_MODIFIERS_MASK, 0x1);
    item->SetState(ITEM_CHANGED, player);

    SendReforgeResult(true);

    if (item->IsEquipped() && !item->IsBroken())
        player->ApplyReforgeEnchantment(item, true);
}


void WorldSession::SendItemUpgradeResult(bool success)
{
    WorldPacket data(SMSG_ITEM_UPGRADE_RESULT, 1);
    data.WriteBit(success);
    data.FlushBits();

    SendPacket(&data);
}

void WorldSession::HandleUpgradeItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network",  "WORLD: Received CMSG_UPGRADE_ITEM");

    ObjectGuid npcGuid;
    ObjectGuid itemGuid;
    Player* player = GetPlayer();

    uint32 item_slot = 0;
    uint32 upgradeEntry = 0;
    uint32 item_bag = 0;

    recvData >> item_bag >> item_slot >> upgradeEntry;

    itemGuid[7] = recvData.ReadBit();
    itemGuid[4] = recvData.ReadBit();
    npcGuid[3] = recvData.ReadBit();
    itemGuid[0] = recvData.ReadBit();
    npcGuid[5] = recvData.ReadBit();
    npcGuid[0] = recvData.ReadBit();
    itemGuid[1] = recvData.ReadBit();
    itemGuid[2] = recvData.ReadBit();
    npcGuid[2] = recvData.ReadBit();
    itemGuid[3] = recvData.ReadBit();
    npcGuid[4] = recvData.ReadBit();
    npcGuid[6] = recvData.ReadBit();
    itemGuid[5] = recvData.ReadBit();
    npcGuid[7] = recvData.ReadBit();
    npcGuid[1] = recvData.ReadBit();
    itemGuid[6] = recvData.ReadBit();

    recvData.ReadByteSeq(itemGuid[6]);
    recvData.ReadByteSeq(itemGuid[1]);
    recvData.ReadByteSeq(npcGuid[7]);
    recvData.ReadByteSeq(itemGuid[5]);
    recvData.ReadByteSeq(itemGuid[4]);
    recvData.ReadByteSeq(npcGuid[6]);
    recvData.ReadByteSeq(itemGuid[0]);
    recvData.ReadByteSeq(npcGuid[3]);
    recvData.ReadByteSeq(itemGuid[7]);
    recvData.ReadByteSeq(npcGuid[2]);
    recvData.ReadByteSeq(npcGuid[4]);
    recvData.ReadByteSeq(npcGuid[5]);
    recvData.ReadByteSeq(itemGuid[3]);
    recvData.ReadByteSeq(npcGuid[1]);
    recvData.ReadByteSeq(npcGuid[0]);
    recvData.ReadByteSeq(itemGuid[2]);

    if (!player->GetNPCIfCanInteractWithFlag2(npcGuid, UNIT_NPC_FLAG2_ITEM_UPGRADE))
    {
        TC_LOG_DEBUG("network", "WORLD: HandleUpgradeItemOpcode - Unit (GUID: %u) not found or player can't interact with it.", GUID_LOPART(npcGuid));
        SendItemUpgradeResult(false);
        return;
    }

    Item* item = player->GetItemByGuid(itemGuid);
    if (!item)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleUpgradeItemOpcode - Item (GUID: %u) not found.", GUID_LOPART(itemGuid));
        SendItemUpgradeResult(false);
        return;
    }

    if (!upgradeEntry)
    {
        // Reset the item.
        if (item->GetUpgradeId() != 0) // If the item has an upgrade id.
        {
            item->SetDynamicUInt32Value(ITEM_DYNAMIC_MODIFIERS, 2, 0);
            item->RemoveFlag(ITEM_FIELD_MODIFIERS_MASK, 0x4);
        }

        item->SetState(ITEM_CHANGED, player);
        SendItemUpgradeResult(false);
        return;
    }

    // Check if item guid is the same as item related to bag and slot
    if (Item* tempItem = player->GetItemByPos(item_bag, item_slot))
    {
        if (item != tempItem)
        {
            TC_LOG_DEBUG("network", "WORLD: HandleUpgradeItemOpcode - Item (GUID: %u) not found.", GUID_LOPART(itemGuid));
            SendItemUpgradeResult(false);
            return;
        }
    }
    else
    {
        TC_LOG_DEBUG("network", "WORLD: HandleUpgradeItemOpcode - Item (GUID: %u) not found.", GUID_LOPART(itemGuid));
        SendItemUpgradeResult(false);
        return;
    }

    ItemUpgradeEntry const* itemUpEntry = sItemUpgradeStore.LookupEntry(upgradeEntry);
    if (!itemUpEntry)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleUpgradeItemOpcode - ItemUpgradeEntry (%u) not found.", upgradeEntry);
        SendItemUpgradeResult(false);
        return;
    }

    // Check if player has enough currency
    if (player->GetCurrency(itemUpEntry->currencyId, false) < itemUpEntry->currencyCost)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleUpgradeItemOpcode - Player has not enougth currency (ID: %u, Cost: %u) not found.", itemUpEntry->currencyId, itemUpEntry->currencyCost);
        SendItemUpgradeResult(false);
        return;
    }

    if (item->GetUpgradeId() != itemUpEntry->precItemUpgradeId)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleUpgradeItemOpcode - ItemUpgradeEntry (%u) is not related to this ItemUpgradePath (%u).", itemUpEntry->Id, item->GetUpgradeId());
        SendItemUpgradeResult(false);
        return;
    }

    item->SetDynamicUInt32Value(ITEM_DYNAMIC_MODIFIERS, 2, itemUpEntry->Id);
    item->SetFlag(ITEM_FIELD_MODIFIERS_MASK, 0x4);
    item->SetState(ITEM_CHANGED, player);

    // Remove currency cost.
    SendItemUpgradeResult(true);

    if (item->IsEquipped())
        player->ApplyItemUpgrade(item, true);

    int32 currencyPointsCost = itemUpEntry->currencyCost;

    player->ModifyCurrency(itemUpEntry->currencyId, -currencyPointsCost, false, true, true);
}
