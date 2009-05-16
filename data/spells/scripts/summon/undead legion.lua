local combat = createCombatObject()
setCombatParam(combat, COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)

local area = createCombatArea(AREA_CIRCLE3X3)
setCombatArea(combat, area)

function onTargetTile(cid, pos)
	local position = pos
	position.stackpos = 255
	local item = getThingfromPos(position)
	if(item.itemid > 0 and isInArray(CORPSES, item.itemid)) then
		doRemoveItem(item.uid,1)
		local creature = doCreateMonster("Skeleton", pos)
		doConvinceCreature(cid, creature)
		doSendMagicEffect(pos, CONST_ME_POFF)
	end
end

setCombatCallback(combat, CALLBACK_PARAM_TARGETTILE, "onTargetTile")

function onCastSpell(cid, var)
	return doCombat(cid, combat, var)
end