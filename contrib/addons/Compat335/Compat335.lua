--[[
    Compat335 - ayuda a que addons escritos para WoW 3.3.5a funcionen en el cliente WoW Classic 3.4.3.

    Se instala como un addon más; los addons 3.3.5 que lo necesiten pueden declararlo en su .toc con
    "## Dependencies: Compat335" (o "## OptionalDeps: Compat335") para cargarse después.

    Qué hace (solo AÑADE lo que falta; no sustituye funciones de Blizzard, para no ensuciar la interfaz segura):
      - SetBackdrop / SetBackdropColor / SetBackdropBorderColor / Get* en marcos creados sin "BackdropTemplate".
      - SetMinResize / SetMaxResize  -> SetResizeBounds.
      - Model:SetLight con los 13 números de 3.3.5 -> tabla de luz de 3.4.3 (las llamadas de Blizzard pasan igual).
      - Funciones globales de bolsas (GetContainerItemInfo, ...) a partir de C_Container, si faltan.
      - SendAddonMessage / RegisterAddonMessagePrefix a partir de C_ChatInfo, si faltan.
      - Plantilla UIPanelButtonTemplate2 (en Compat335.xml).

    Lo que NO puede arreglar desde fuera (hay que cambiarlo en el addon; ver README.md):
      - PlaySound("nombre"): en 3.4.3 solo acepta números -> PlaySound(SOUNDKIT.NOMBRE).
      - DressUpModel:TryOn(idDeObjeto): en 3.4.3 un número es un ItemModifiedAppearanceID -> TryOn("item:" .. id).
      - El evento de modelo "OnUpdateModel" ya no existe -> "OnModelLoaded".
      - Variables implícitas this / arg1..argN en scripts: usar los parámetros del manejador.
]]

if type(C_ChatInfo) ~= "table" then
    return -- cliente 3.3.5: no hace falta nada
end

-- ------------------------------------------------------------------ métodos que faltan en los tipos de marco
local TIPOS = {
    "Frame", "Button", "CheckButton", "EditBox", "ScrollFrame", "Slider", "StatusBar",
    "Model", "PlayerModel", "DressUpModel", "ColorSelect", "MessageFrame", "ScrollingMessageFrame", "SimpleHTML",
}

local function PreparaFondo(self)
    if not rawget(self, "__c335Fondo") then
        rawset(self, "__c335Fondo", true)
        Mixin(self, BackdropTemplateMixin)
        if self.HookScript and self.OnBackdropSizeChanged then
            self:HookScript("OnSizeChanged", self.OnBackdropSizeChanged)
        end
    end
end

local METODOS_FONDO = {
    "SetBackdrop", "GetBackdrop", "SetBackdropColor", "GetBackdropColor", "SetBackdropBorderColor", "GetBackdropBorderColor",
}

local function Limites(self)
    local l = rawget(self, "__c335Limites")
    if not l then
        l = { 0, 0, 0, 0 }
        rawset(self, "__c335Limites", l)
    end
    return l
end

-- Model:SetLight 3.3.5: (activa, omni, dirX, dirY, dirZ, ambIntensidad, ambR, ambG, ambB, dirIntensidad, dirR, dirG, dirB)
local function Luz343(enabled, omni, x, y, z, ambI, ambR, ambG, ambB, difI, difR, difG, difB)
    return (enabled == true or enabled == 1), {
        omnidirectional = (omni == true or omni == 1),
        point = CreateVector3D(x or 0, y or 0, z or 0),
        ambientIntensity = ambI or 0,
        ambientColor = CreateColor(ambR or 0, ambG or 0, ambB or 0),
        diffuseIntensity = difI or 0,
        diffuseColor = CreateColor(difR or 0, difG or 0, difB or 0),
    }
end

for _, tipo in ipairs(TIPOS) do
    local ok, marco = pcall(CreateFrame, tipo)
    if ok and marco then
        local mt = getmetatable(marco)
        local indice = mt and mt.__index
        if type(indice) == "table" then
            if type(BackdropTemplateMixin) == "table" then
                for _, nombre in ipairs(METODOS_FONDO) do
                    if indice[nombre] == nil and BackdropTemplateMixin[nombre] then
                        local original = BackdropTemplateMixin[nombre]
                        indice[nombre] = function(self, ...)
                            PreparaFondo(self)
                            return original(self, ...)
                        end
                    end
                end
            end
            if indice.SetMinResize == nil and indice.SetResizeBounds then
                indice.SetMinResize = function(self, w, h)
                    local l = Limites(self); l[1], l[2] = w or 0, h or 0
                    self:SetResizeBounds(l[1], l[2], l[3] > 0 and l[3] or nil, l[4] > 0 and l[4] or nil)
                end
                indice.SetMaxResize = function(self, w, h)
                    local l = Limites(self); l[3], l[4] = w or 0, h or 0
                    self:SetResizeBounds(l[1], l[2], l[3] > 0 and l[3] or nil, l[4] > 0 and l[4] or nil)
                end
            end
            local setLight = indice.SetLight
            if setLight and not rawget(indice, "__c335Luz") and type(CreateVector3D) == "function" then
                indice.__c335Luz = true
                indice.SetLight = function(self, enabled, segundo, ...)
                    if type(segundo) == "table" or segundo == nil then
                        return setLight(self, enabled, segundo, ...)
                    end
                    return setLight(self, Luz343(enabled, segundo, ...))
                end
            end
        end
        if marco.Hide then marco:Hide() end
    end
end

-- ------------------------------------------------------------------ funciones globales que pasaron a C_*
local function SiFalta(nombre, funcion)
    if _G[nombre] == nil and funcion then
        _G[nombre] = funcion
    end
end

if type(C_Container) == "table" then
    local C = C_Container
    SiFalta("GetContainerNumSlots", C.GetContainerNumSlots)
    SiFalta("GetContainerNumFreeSlots", C.GetContainerNumFreeSlots)
    SiFalta("GetContainerItemLink", C.GetContainerItemLink)
    SiFalta("GetContainerItemID", C.GetContainerItemID)
    SiFalta("PickupContainerItem", C.PickupContainerItem)
    SiFalta("SplitContainerItem", C.SplitContainerItem)
    SiFalta("UseContainerItem", C.UseContainerItem)
    if C.GetContainerItemInfo then
        -- 3.3.5: texture, count, locked, quality, readable, lootable, link (varios valores); 3.4.3 devuelve una tabla
        SiFalta("GetContainerItemInfo", function(bolsa, hueco)
            local i = C.GetContainerItemInfo(bolsa, hueco)
            if not i then return nil end
            return i.iconFileID, i.stackCount, i.isLocked, i.quality, i.isReadable, i.hasLoot, i.hyperlink,
                   i.isFiltered, i.hasNoValue, i.itemID, i.isBound
        end)
    end
end

SiFalta("SendAddonMessage", C_ChatInfo.SendAddonMessage)
SiFalta("RegisterAddonMessagePrefix", C_ChatInfo.RegisterAddonMessagePrefix)
