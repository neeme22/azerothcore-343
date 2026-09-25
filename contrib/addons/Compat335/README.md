# Compat335: addons de 3.3.5a en el cliente 3.4.3

Copia la carpeta `Compat335` a `_classic_\Interface\AddOns\`. Para el addon que quieras portar:

1. Crea `NombreAddon_Wrath.toc` junto a su `.toc`, con `## Interface: 30403` y `## Dependencies: Compat335` (así el
   cliente no lo marca como desfasado y se carga después de la librería). El `.toc` original sigue valiendo en 3.3.5.
2. Entra con `/console scriptErrors 1` y abre las ventanas del addon: los errores dicen qué falta.

## Qué arregla Compat335 solo

| Error típico en 3.4.3 | Causa |
|---|---|
| `attempt to call method 'SetBackdrop' (a nil value)` | los marcos ya no traen fondo si no heredan `BackdropTemplate` |
| `attempt to call method 'SetMinResize'` / `SetMaxResize` | ahora es `SetResizeBounds` |
| el modelo sale negro o sin luz tras `SetLight(1, 0, ...)` | `SetLight` recibe una tabla en 3.4.3 |
| `attempt to call global 'GetContainerItemInfo'` (y demás de bolsas) | pasaron a `C_Container` |
| `attempt to call global 'SendAddonMessage'` | pasó a `C_ChatInfo` |
| `Couldn't find inherited node "UIPanelButtonTemplate2"` | la plantilla ya no existe |

## Qué hay que cambiar en el addon

| En 3.3.5 | En 3.4.3 |
|---|---|
| `PlaySound("gsTitleOptionOK")` | `PlaySound(SOUNDKIT.GS_TITLE_OPTION_OK)` (nombres en `SoundKitConstants.lua` de la interfaz) |
| `modelo:TryOn(idObjeto)` | `modelo:TryOn("item:" .. idObjeto)`: con un número 3.4.3 espera un `ItemModifiedAppearanceID` |
| `modelo:SetScript("OnUpdateModel", f)` | `modelo:SetScript("OnModelLoaded", f)` |
| `this`, `arg1`... dentro de scripts | los parámetros del manejador: `function(self, event, ...)` |
| `CHAT_MSG_ADDON` con emisor `"Nombre"` | llega como `"Nombre-Reino"`: compara con `Ambiguate(emisor, "none")` |

Compat335 no sustituye funciones de Blizzard (solo añade las que faltan), para no ensuciar la interfaz segura: por eso
`PlaySound` y `TryOn` hay que cambiarlos en el propio addon.

## Addons que usan AIO

Instala también `contrib/aio/AIO_Client` (ya trae su propia compatibilidad) y los scripts de `contrib/aio/AIO_Server` en
el servidor. Con eso el transmog AppearanceBuddy funcionó con estos cambios: `_Wrath.toc`, la plantilla, fondos,
`SetMinResize`, `SetLight`, 45 llamadas a `PlaySound`, cuatro `TryOn` y un `OnUpdateModel`.
