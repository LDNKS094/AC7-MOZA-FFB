-- AC7 producer for the unmodified AceBridge / DCS.exe protocol.
-- UObject reads stay on the game thread; only pipe I/O runs on the async worker.
local UEHelpers = require("UEHelpers")
local GUN_HELD_OFFSET = 676
local VELOCITY_SCALE = 10.0
local PERIOD_MS = 16
local generation = (_G.AC7TelemetryGeneration or 0) + 1
_G.AC7TelemetryGeneration = generation

local function log(text) print("[AC7Telemetry] " .. text .. "\n") end
local function valid(object) return object and object:IsValid() end
local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) < math.huge
end

assert(type(LoopInGameThreadWithDelay) == "function", "UE4SS game-thread timers required")
assert(type(RegisterCustomProperty) == "function", "UE4SS custom properties required")

-- This is a byte written by the actual native weapon implementation, not an input hook.
RegisterCustomProperty({
    Name = "AC7NativeGunHeld",
    Type = PropertyTypes.Int8Property,
    BelongsToClass = "/Script/Nimbus.PlayerWeaponActivator",
    OffsetInternal = GUN_HELD_OFFSET,
})

local latest = "none\n"
local player_name, last_error = "none", nil
local last_report_at, last_gun, last_paused = 0, nil, nil
local gameplay = StaticFindObject("/Script/Engine.Default__GameplayStatics")
assert(valid(gameplay), "GameplayStatics default object unavailable")
local sampled_at = 0

local function sample()
    sampled_at = os.time()
    local pc = UEHelpers.GetPlayerController()
    local pawn = valid(pc) and pc.Pawn or nil
    if not valid(pawn) or not pawn:IsA("/Script/Nimbus.PlayerPlane") then
        latest = "none\n"
        if player_name ~= "none" then
            player_name, last_gun, last_paused = "none", nil, nil
            log("no player aircraft")
        end
        return
    end
    local identity = pawn:GetFullName()
    local class = pawn:GetClass():GetFName():ToString()
    if identity ~= player_name then
        player_name = identity
        log("aircraft=" .. class .. " object=" .. identity .. "; Cockpit profile=AC7 Default")
    end
    local paused = gameplay:IsGamePaused(pawn)
    local velocity = pawn:GetVelocity()
    local rotation = pawn:K2_GetActorRotation()
    local vx, vy, vz = velocity.X * VELOCITY_SCALE, velocity.Y * VELOCITY_SCALE, velocity.Z * VELOCITY_SCALE
    local speed_kmh = pawn:GetSpeedKmph()
    local health, maximum = pawn:GetHealth(), pawn:GetMaxHealth()
    local cloud, throttle = pawn.CurrentCloudDensity, pawn.InputThrottle
    local activator = pawn.PlayerWeaponActivator
    local held = valid(activator) and activator.AC7NativeGunHeld or 0
    assert(held == 0 or held == 1, "invalid native gun state")
    local gun = not paused and held == 1 and pawn.bEnableWeaponInput and not pawn.bIsWeaponSystemDisabled and health > 0
    for _, value in ipairs({vx, vy, vz, rotation.Pitch, rotation.Yaw, rotation.Roll, speed_kmh, health, maximum, cloud, throttle}) do
        assert(finite(value), "invalid telemetry scalar")
    end
    assert(maximum > 0 and speed_kmh >= 0, "invalid health/speed range")
    cloud = math.max(0, math.min(1, cloud))
    throttle = math.max(0, math.min(1, throttle)) -- same input-throttle semantics as the AC8 producer
    -- shortcut: icing state is unverified; ic stays zero until its actual source is confirmed.
    latest = paused and "none\n" or string.format(
        "pl=%s;spd=%.3f;vx=%.3f;vy=%.3f;vz=%.3f;p=%.3f;y=%.3f;r=%.3f;" ..
        "thr=%.3f;hp=%.3f;mhp=%.3f;gf=%d;cl=%d;cd=%.4f;ic=0;\n",
        class, speed_kmh / 3.6 * 100, vx, vy, vz, rotation.Pitch, rotation.Yaw, rotation.Roll,
        throttle, health, maximum, gun and 1 or 0, cloud > 0.01 and 1 or 0, cloud)
    if gun ~= last_gun then last_gun = gun; log("gun=" .. (gun and "1" or "0")) end
    if paused ~= last_paused then last_paused = paused; log("paused=" .. tostring(paused)) end
    if sampled_at - last_report_at >= 2 then
        last_report_at = sampled_at
        log(string.format("live speed=%.1f km/h hp=%.1f/%.1f thr=%.2f gun=%d cloud=%.2f paused=%s",
            speed_kmh, health, maximum, throttle, gun and 1 or 0, cloud, tostring(paused)))
    end
end

LoopInGameThreadWithDelay(PERIOD_MS, function()
    if _G.AC7TelemetryGeneration ~= generation then return end
    local ok, err = pcall(sample)
    if not ok then
        latest = "none\n"
        if tostring(err) ~= last_error then last_error = tostring(err); log("read error: " .. last_error) end
    end
end)

local pipe, attempts = nil, 0
LoopAsync(PERIOD_MS, function()
    if _G.AC7TelemetryGeneration ~= generation then
        if pipe then pcall(function() pipe:close() end) end
        return true
    end
    if not pipe then
        attempts = attempts + 1
        if attempts == 1 or attempts % 120 == 0 then
            pipe = io.open("\\\\.\\pipe\\AceTelemetry", "w")
            if pipe then pipe:setvbuf("no"); log("connected to AceBridge") end
        end
        if not pipe then return false end
    end
    local frame = os.difftime(os.time(), sampled_at) > 2 and "none\n" or latest
    local ok = pcall(function() assert(pipe:write(frame), "pipe write failed") end)
    if not ok then
        pcall(function() pipe:close() end)
        pipe, attempts = nil, 0
        log("bridge disconnected; waiting for DCS.exe")
    end
    return false
end)
log("loaded: game-thread telemetry, native gun state, original AceTelemetry protocol")
