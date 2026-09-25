--[[
    AIO_Compat343.lua

    Capa de compatibilidad para que AIO 1.75 (Rochet2/AIO, pensado para 3.3.5)
    corra en el cliente WoW Classic 3.4.3.54261 conectado a través de worldgate.

    Se carga ANTES de AIO.lua (ver AIO_Client_Wrath.toc). En un cliente 3.3.5 no
    hace nada: allí C_ChatInfo no existe y las globales antiguas sí.

    Qué arregla:
      1. SendAddonMessage / RegisterAddonMessagePrefix globales ya no existen en
         3.4.3 (solo C_ChatInfo.*). AIO.lua:491 llama a la global.
      2. AIO.lua:1131-1134 solo registra el prefijo si tocversion >= 40100; en
         3.4.3 GetBuildInfo() da 30403, así que nunca registra nada. Aquí se
         registran los dos prefijos (el cliente moderno puede no disparar
         CHAT_MSG_ADDON para prefijos no registrados).
      3. _ERRORMESSAGE ya no existe (AIO.lua:394, solo con AIO_ENABLE_TRACEBACK).

    Lo que no se puede arreglar desde fuera ya va aplicado en AIO.lua de este repositorio:
      - AIO.lua:1033 compara sender == UnitName("player"); en 3.4.3 el sender de
        CHAT_MSG_ADDON llega como "Nombre-Reino". Hay que comparar con Ambiguate.
]]

local C = C_ChatInfo
if type(C) ~= "table" then
    return -- cliente 3.3.5: nada que hacer
end

-- 1) globales antiguas a partir de C_ChatInfo (solo si faltan)
if not SendAddonMessage and C.SendAddonMessage then
    function SendAddonMessage(prefix, text, chatType, target)
        return C.SendAddonMessage(prefix, text, chatType, target)
    end
end
if not RegisterAddonMessagePrefix and C.RegisterAddonMessagePrefix then
    function RegisterAddonMessagePrefix(prefix)
        return C.RegisterAddonMessagePrefix(prefix)
    end
end

-- 2) prefijos de AIO: "S".."AIO" (servidor -> cliente) y "C".."AIO" (eco cliente -> cliente)
--    Deben coincidir con AIO_Prefix de AIO.lua:259 si alguien lo cambia.
if C.RegisterAddonMessagePrefix then
    C.RegisterAddonMessagePrefix("SAIO")
    C.RegisterAddonMessagePrefix("CAIO")
end

-- 3) _ERRORMESSAGE (solo lo usa AIO con /aio trace activado)
if not _ERRORMESSAGE then
    function _ERRORMESSAGE(msg)
        local h = geterrorhandler and geterrorhandler()
        if h then h(msg) else print(msg) end
    end
end
