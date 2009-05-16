local config = {
	deathListEnabled = getBooleanFromString(getConfigInfo('deathListEnabled')),
	sqlType = getConfigInfo('sqlType'),
	maxDeathRecords = getConfigInfo('maxDeathRecords')
}

config.sqlType = config.sqlType == "sqlite" and DATABASE_ENGINE_SQLITE or DATABASE_ENGINE_MYSQL

function onDeath(cid, corpse, lastHitKiller, mostDamageKiller)
	if(not config.deathListEnabled) then
		return
	end

	local hitKillerName = "field item"
	local damageKillerName = ""
	if(lastHitKiller ~= 0) then
		if(isPlayer(lastHitKiller)) then
			hitKillerName = getPlayerGUID(lastHitKiller)
		else
			hitKillerName = getCreatureName(lastHitKiller)
		end

		if(mostDamageKiller ~= 0 and mostDamageKiller ~= lastHitKiller and getCreatureName(mostDamageKiller) ~= getCreatureName(lastHitKiller)) then
			if(isPlayer(mostDamageKiller)) then
				damageKillerName = getPlayerGUID(mostDamageKiller)
			else
				damageKillerName = getCreatureName(mostDamageKiller)
			end
		end
	end

	db.executeQuery("INSERT INTO `player_deaths` (`player_id`, `time`, `level`, `killed_by`, `altkilled_by`) VALUES (" .. getPlayerGUID(cid) .. ", " .. os.time() .. ", " .. getPlayerLevel(cid) .. ", " .. db.escapeString(hitKillerName) .. ", " .. db.escapeString(damageKillerName) .. ");")
	local rows = db.getResult("SELECT `player_id` FROM `player_deaths` WHERE `player_id` = " .. getPlayerGUID(cid) .. ";")
	if(rows:getID() ~= -1) then
		local amount = rows:getRows(true) - config.maxDeathRecords
		if(amount > 0) then
			if(config.sqlType == DATABASE_ENGINE_SQLITE) then
				for i = 1, amount do
					db.executeQuery("DELETE FROM `player_deaths` WHERE `rowid` = (SELECT `rowid` FROM `player_deaths` WHERE `player_id` = " .. getPlayerGUID(cid) .. " ORDER BY `time` LIMIT 1);")
				end
			else
				db.executeQuery("DELETE FROM `player_deaths` WHERE `player_id` = " .. getPlayerGUID(cid) .. " ORDER BY `time` LIMIT " .. amount .. ";")
			end
		end
	end
end
