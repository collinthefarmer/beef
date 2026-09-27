Scriptname Actor extends ObjectReference Hidden
Function EquipItem(Form item, Bool preventRemoval = False, Bool silent = False) Native
Function UnequipItem(Form item, Bool preventEquip = False, Bool silent = False) Native
Bool Function IsEquipped(Form item) Native
Form Function GetWornForm(Int slotMask) Native
