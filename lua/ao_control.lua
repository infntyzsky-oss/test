local ffi = require('ffi')

ffi.cdef[[
    void set_ao_intensity(float intensity);
    void set_ao_enabled(int enabled);
    float get_ao_intensity();
    int get_injection_count();
]]

local ao_lib = nil
local ao_state = {
    enabled = true,
    intensity = 5
}

local function init_ao_lib()
    local status, lib = pcall(function()
        return ffi.load('ao_inject')
    end)
    
    if status then
        ao_lib = lib
        return true
    else
        sampAddChatMessage('[AO] Failed to load library: ' .. tostring(lib), 0xFF0000)
        return false
    end
end

function main()
    repeat wait(0) until isSampAvailable()
    wait(3000)
    
    if not init_ao_lib() then
        sampAddChatMessage('[AO] Library not found - make sure libao_inject.so is installed', 0xFF0000)
        return
    end
    
    sampRegisterChatCommand('ao', function(param)
        if param == '' then
            ao_state.enabled = not ao_state.enabled
            ao_lib.set_ao_enabled(ao_state.enabled and 1 or 0)
            sampAddChatMessage('[AO] ' .. (ao_state.enabled and 'ENABLED' or 'DISABLED'), 0x00FF00)
        else
            local intensity = tonumber(param)
            if intensity and intensity >= 0 and intensity <= 10 then
                ao_state.intensity = intensity
                local normalized = intensity / 10.0
                ao_lib.set_ao_intensity(normalized)
                
                local level = intensity >= 8 and 'NORAK' or 
                             intensity >= 5 and 'BALANCED' or
                             intensity > 0 and 'SUBTLE' or 'OFF'
                             
                sampAddChatMessage(string.format('[AO] Intensity: %d/10 (%s)', intensity, level), 0x00FF00)
            else
                sampAddChatMessage('[AO] Usage: /ao [0-10]', 0xFF0000)
                sampAddChatMessage('[AO] 0 = off, 5 = balanced, 10 = maximum', 0xFFFFFF)
            end
        end
    end)
    
    sampRegisterChatCommand('aostats', function()
        local current = ao_lib.get_ao_intensity()
        local count = ao_lib.get_injection_count()
        sampAddChatMessage(string.format('[AO] Intensity: %.2f | Injections: %d', current, count), 0x00FFFF)
    end)
    
    sampAddChatMessage('[AO] Shader library loaded successfully!', 0x00FF00)
    sampAddChatMessage('[AO] /ao - toggle | /ao [0-10] - set intensity', 0xFFFFFF)
    sampAddChatMessage('[AO] /aostats - show statistics', 0xFFFFFF)
    
    while true do wait(0) end
end
