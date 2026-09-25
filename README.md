# AzerothCore 3.4.3 — Wrath Classic client support for AzerothCore

**[English](#english) · [Español](#español)**

AzerothCore (Playerbot branch) playable with the **World of Warcraft: Wrath of the Lich King Classic 3.4.3.54261**
client. Open source, made to be improved together.

AzerothCore (rama Playerbot) jugable con el cliente **World of Warcraft: Wrath of the Lich King Classic 3.4.3.54261**.
Código abierto, hecho para mejorarlo entre todos.

```
3.4.3 client ──login──▶ bnetserver (1119, REST 8081) ──▶ auth.realmlist (3.4.3 realm, port 8086)
3.4.3 client ──game───▶ worldgate  (8086/8087) ──3.3.5 protocol──▶ AzerothCore worldserver (8085)
```

---

## English

### What is this

This is **AzerothCore** — the open-source World of Warcraft 3.3.5a server — made playable with the modern
**WotLK Classic 3.4.3.54261** client (launched through **Arctium WoW Launcher**).

The world server is still stock AzerothCore 3.3.5a with its **original database**. Two pieces make the new client work:

- **`bnetserver`** (ported from TrinityCore 3.4.3): the Battle.net login the modern client uses. Your AzerothCore
  accounts work as they are; the Battle.net account is created automatically the first time you log in.
- **`worldgate`**: a gateway that translates the 3.4.3 protocol to AzerothCore's 3.3.5 protocol in real time, and
  sends the client the data it needs as **hotfixes** (items, spells and texts with AzerothCore's values).

### Modules and extras included

| Part | Where | What it is |
|---|---|---|
| Core | `src/` | AzerothCore Playerbot (`mod-playerbots/azerothcore-wotlk`, commit `413bea61a`) + 3.4.3 support |
| bnetserver | `src/server/apps/bnetserver` | Battle.net login for the 3.4.3 client (ported from TrinityCore 3.4.3) |
| Gateway | `gateway/` | `worldgate` and the TrinityCore 3.4.3 (TDB343.24081) it uses for packets and DB2 data |
| **mod-playerbots** | `modules/` | AI-controlled players (random bots and your own alts) |
| **mod-ale** (Eluna) | `modules/` | Lua scripting engine for the world server |
| **mod-individual-progression** | `modules/` | Vanilla → TBC → WotLK progression per character |
| **mod-spellregulator** | `modules/` | per-spell damage/healing tuning from the database |
| AIO | `contrib/aio` | AIO 1.75 (server-driven addons) with 3.4.3 client support |
| Compat335 | `contrib/addons/Compat335` | helper library to port addons written for 3.3.5a to the 3.4.3 API |
| Database | `data/sql` | AzerothCore's original database + Battle.net tables and the 3.4.3 realm |
| Hotfixes | `gateway/sql` | hotfix database the gateway sends to the client (AzerothCore items, spells and texts) |

Non-protocol changes to AzerothCore: `GM.AreaTriggerScripts` (GMs also trigger scripted portals), the Putricide
laboratory door in ICC, the `mod-spellregulator` integration and a larger `SPELL_LINKED_MAX_SPELLS`. The 3.3.5
`authserver` is disabled: the 3.4.3 client logs in through `bnetserver`.

### Requirements

- **WoW Classic 3.4.3.54261** client (exactly that build) and **Arctium WoW Launcher**.
- Same as AzerothCore: CMake ≥ 3.16, Visual Studio 2022 or newer (or GCC/Clang), Boost ≥ 1.78, OpenSSL 3, MySQL 8.
  See <https://www.azerothcore.org/wiki/installation>.
- AzerothCore 3.3.5 data (dbc, maps, vmaps, mmaps): [wowgaming/client-data](https://github.com/wowgaming/client-data/releases).

### Quick start tutorial

**1. Build the server (AzerothCore + bnetserver)**

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<server folder> -DSCRIPTS=static -DAPPS_BUILD=all -DTOOLS_BUILD=db-only
cmake --build build --config Release --target install
```

You get `worldserver`, `bnetserver` and `dbimport`. The modules in `modules/` are built automatically.

**2. Build the gateway (worldgate + extractor)**

```bash
cmake -S gateway/trinitycore-343 -B build-gateway -DSERVERS=1 -DTOOLS=1 -DSCRIPTS=static -DWITH_WARNINGS=0
cmake --build build-gateway --config Release --target worldgate mapextractor
```

`worldgate.conf.dist` and the files from `gateway/data` are copied next to `worldgate`.

**3. Databases**

1. Create the AzerothCore databases as usual (`dbimport`, or the first world server start with
   `Updates.AutoSetup = 1`). The Battle.net tables, the 3.4.3 realm and the modules' SQL are applied automatically.
2. Create the gateway's two databases and give the AzerothCore user access to them: `tc343_hotfixes` (the hotfixes
   sent to the client) and `tc343_auth` (TrinityCore permissions the gateway uses internally; no accounts):

   ```bash
   mysql -uroot -e "CREATE DATABASE tc343_hotfixes DEFAULT CHARSET utf8mb4; CREATE DATABASE tc343_auth DEFAULT CHARSET utf8mb4; GRANT ALL ON tc343_hotfixes.* TO 'acore'@'localhost'; GRANT ALL ON tc343_auth.* TO 'acore'@'localhost';"
   gunzip -c gateway/sql/tc343_hotfixes.sql.gz | mysql -uroot tc343_hotfixes
   mysql -uroot tc343_auth < gateway/sql/tc343_auth.sql
   ```

3. If the server is not on 127.0.0.1, change `address`/`localAddress` of the 3.4.3 realm (`acore_auth.realmlist`, id 2).

**4. Server data: 3.3.5 DBC, maps, vmaps and mmaps**

The world server is AzerothCore 3.3.5, so it works with **3.3.5 DBC files** and map data, exactly like any
AzerothCore server:

- `dbc/`: the 3.3.5 game tables (spells, items, creatures, areas...).
- `maps/`: terrain heights and liquids.
- `vmaps/`: buildings and objects (line of sight and collisions).
- `mmaps/`: navigation meshes (how creatures and bots move).
- `Cameras/`: cinematic cameras.

Download the ready-made `Data.zip` from [wowgaming/client-data](https://github.com/wowgaming/client-data/releases)
(the release that matches AzerothCore) and extract it into the world server's data folder: `DataDir` in
`worldserver.conf`, by default a `data` folder next to `worldserver`. You should end up with
`data/dbc`, `data/maps`, `data/vmaps`, `data/mmaps` and `data/Cameras`.

> **Why two sets of data?** The world server only uses the **3.3.5 DBC** data above. The gateway translates for the
> 3.4.3 client, so it needs to know what that client has: the **3.4.3 DB2** files of step 5. The server logic never
> reads DB2, and the gateway never reads the 3.3.5 DBC.

**5. 3.4.3 client data for the gateway (DB2)**

The gateway needs the DB2 files of your own client. Copy `mapextractor` (from `build-gateway`) to the WoW Classic
install folder (the one that contains `_classic_`) and run `mapextractor -e 2` (DB2 only; maps are not needed). It
creates `dbc/<locale>/` (e.g. `dbc/enUS/`); put that `dbc` folder inside a `data343` folder next to `worldgate`
(or point `DataDir` in `worldgate.conf` to the folder that contains `dbc`).

Any client language works: the gateway uses `dbc/enUS` if it exists and otherwise the first language it finds; to
force one, set `Locale` in `worldgate.conf` (e.g. `Locale = esES`). If it finds no DB2 it stops with an error that
says where it looked.

**6. Configuration**

| File | What to check |
|---|---|
| `worldserver.conf` | the usual AzerothCore settings (`WorldServerPort = 8085`, `RealmID = 1`) and `DataDir`: the folder with the 3.3.5 data (`dbc/`, `maps/`, `vmaps/`, `mmaps/`, `Cameras/` from client-data; by default `data` next to the worldserver) |
| `bnetserver.conf` | `LoginDatabaseInfo`; `LoginREST.ExternalAddress`/`LocalAddress` with the server IP |
| `worldgate.conf` | copy of `worldgate.conf.dist`: databases, `DataDir` and `WorldServerConf` (path to your `worldserver.conf`) |

`bnetserver` needs `bnetserver.cert.pem` and `bnetserver.key.pem` next to it (TrinityCore's public certificates,
the ones Arctium accepts).

**7. Start and log in**

1. Start MySQL, `worldserver`, `bnetserver` and `worldgate` (any order; the gateway connects to the world server when a
   player logs in).
2. Create your account in the world server console: `.account create USER PASSWORD` (and `.account set gmlevel` if you
   want).
3. In the client, `_classic_/WTF/Config.wtf` must contain `SET portal "127.0.0.1"` (or your server IP).
4. Open the game with **Arctium WoW Launcher** from the install folder and log in with `USER` and `PASSWORD`.

### Ports

| Service | Port |
|---|---|
| bnetserver (Battle.net / REST) | 1119 / 8081 |
| worldgate (3.4.3 client) | 8086 and 8087 |
| worldserver (AzerothCore) | 8085 |

### Regenerating the hotfixes

The generators are in `gateway/trinitycore-343/src/server/worldgate` (Python 3). They are configured with environment
variables: `WG_MYSQL`, `WG_DB_HOST`, `WG_DB_PORT`, `WG_DB_USER`, `WG_DB_PASS`, `WG_WORLD_DB`, `WG_HOTFIX_DB`,
`WG_AC_DATA` (world server data folder, the one with `dbc`) and `WG_DB2_DIR` (3.4.3 client DB2).

| Script | What it generates |
|---|---|
| `gen_hotfix_objetos_ac.py` | client items with the values from `item_template` (ItemSparse/Item/ItemEffect) |
| `gen_hotfix_extendedcost_ac.py` | honor, arena and token prices of vendors |
| `valida_hotfix_ac.py file.sql` | checks a generated SQL against the hotfix database |
| `gen_monturas.py`, `gen_auras_puntos.py` | gateway tables for mount and aura tooltips |

If you write `item_sparse` rows by hand: the 3.4.3 client adds `QualityModifier` to weapon damage (as DPS) and to
armor, and without flag `0x4000` in `Flags2` it shows a calculated price instead of `SellPrice`/`BuyPrice`. To show
exact values: `QualityModifier = 0` and `Flags2 |= 0x4000`.

### AIO

`contrib/aio/AIO_Server` goes into the world server's `lua_scripts` folder (Eluna/ALE). `contrib/aio/AIO_Client` is
the client addon: copy it to `_classic_/Interface/AddOns/AIO_Client` (it includes `AIO_Client_Wrath.toc` for 3.4.3 and
the `AIO_Compat343.lua` layer). Addons sent through AIO must use the 3.4.3 API; to port 3.3.5 addons, see
`contrib/addons/Compat335/README.md`.

### Bugs, feedback and contributing

This project is **open source** and it is far from finished: there **will** be bugs, and that is exactly why it is
public. Any feedback is welcome, and **anyone can fix things and keep it going** — together we can make it better.

- **Found a bug?** Open an **Issue** with what you did, what you expected and what happened. Attach the logs:
  `worldgate.log` (gateway), `Server.log`/`Errors.log` (world server) and, if the client crashed, the `.txt` from
  `_classic_/Errors`.
- **Fixed something?** Pull requests are welcome. Keep changes focused and explain how you tested them.
- **Want to help?** Pick any open issue, or test a zone, a dungeon or a class and report what does not work.

### Licenses

AzerothCore and TrinityCore: GPL-2.0 (see `LICENSE` and `gateway/trinitycore-343/COPYING`). Each module and AIO keep
their own license in their folder. World of Warcraft is a trademark of Blizzard Entertainment; this project does not
include any client file.

---

## Español

### Qué es esto

Es **AzerothCore** —el servidor de código abierto de World of Warcraft 3.3.5a— hecho jugable con el cliente moderno
**WotLK Classic 3.4.3.54261** (abierto con **Arctium WoW Launcher**).

El servidor de mundo sigue siendo AzerothCore 3.3.5a con su **base de datos original**. Dos piezas hacen que funcione
el cliente nuevo:

- **`bnetserver`** (portado de TrinityCore 3.4.3): el login de Battle.net que usa el cliente moderno. Tus cuentas de
  AzerothCore valen tal cual; la cuenta de Battle.net se crea sola la primera vez que entras.
- **`worldgate`**: una pasarela que traduce en tiempo real el protocolo 3.4.3 al 3.3.5 de AzerothCore y manda al
  cliente los datos que necesita como **hotfixes** (objetos, hechizos y textos con los valores de AzerothCore).

### Módulos y extras incluidos

| Parte | Dónde | Qué es |
|---|---|---|
| Núcleo | `src/` | AzerothCore Playerbot (`mod-playerbots/azerothcore-wotlk`, commit `413bea61a`) + soporte 3.4.3 |
| bnetserver | `src/server/apps/bnetserver` | login Battle.net del cliente 3.4.3 (portado de TrinityCore 3.4.3) |
| Pasarela | `gateway/` | `worldgate` y el TrinityCore 3.4.3 (TDB343.24081) que usa para los paquetes y los DB2 |
| **mod-playerbots** | `modules/` | jugadores controlados por IA (bots aleatorios y tus propios personajes) |
| **mod-ale** (Eluna) | `modules/` | motor de scripts Lua para el servidor de mundo |
| **mod-individual-progression** | `modules/` | progresión Vanilla → TBC → WotLK por personaje |
| **mod-spellregulator** | `modules/` | ajuste del daño y la curación de cada hechizo desde la base de datos |
| AIO | `contrib/aio` | AIO 1.75 (addons mandados por el servidor) compatible con el cliente 3.4.3 |
| Compat335 | `contrib/addons/Compat335` | librería para portar addons escritos para 3.3.5a a la API de 3.4.3 |
| Base de datos | `data/sql` | la original de AzerothCore + tablas de Battle.net y el reino 3.4.3 |
| Hotfixes | `gateway/sql` | base de hotfixes que la pasarela manda al cliente (objetos, hechizos y textos de AzerothCore) |

Cambios sobre AzerothCore que no son del protocolo: `GM.AreaTriggerScripts` (los GM también disparan portales por
script), la puerta del laboratorio de Putricidio en ICC, la integración de `mod-spellregulator` y
`SPELL_LINKED_MAX_SPELLS` ampliado. El `authserver` 3.3.5 viene desactivado: el cliente 3.4.3 entra por `bnetserver`.

### Requisitos

- Cliente **WoW Classic 3.4.3.54261** (exactamente ese build) y **Arctium WoW Launcher**.
- Los mismos que AzerothCore: CMake ≥ 3.16, Visual Studio 2022 o posterior (o GCC/Clang), Boost ≥ 1.78, OpenSSL 3,
  MySQL 8. Ver <https://www.azerothcore.org/wiki/installation>.
- Datos 3.3.5 de AzerothCore (dbc, maps, vmaps, mmaps): [wowgaming/client-data](https://github.com/wowgaming/client-data/releases).

### Tutorial de arranque rápido

**1. Compilar el servidor (AzerothCore + bnetserver)**

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<carpeta del servidor> -DSCRIPTS=static -DAPPS_BUILD=all -DTOOLS_BUILD=db-only
cmake --build build --config Release --target install
```

Salen `worldserver`, `bnetserver` y `dbimport`. Los módulos de `modules/` se compilan solos.

**2. Compilar la pasarela (worldgate + extractor)**

```bash
cmake -S gateway/trinitycore-343 -B build-gateway -DSERVERS=1 -DTOOLS=1 -DSCRIPTS=static -DWITH_WARNINGS=0
cmake --build build-gateway --config Release --target worldgate mapextractor
```

Junto a `worldgate` quedan copiados `worldgate.conf.dist` y los ficheros de datos de `gateway/data`.

**3. Bases de datos**

1. Crea las bases de AzerothCore como siempre (`dbimport` o el primer arranque del worldserver con
   `Updates.AutoSetup = 1`). Se aplican solas las tablas de Battle.net, el reino 3.4.3 y el SQL de los módulos.
2. Crea las dos bases de la pasarela y dale permisos al usuario de AzerothCore: `tc343_hotfixes` (los hotfixes que se
   mandan al cliente) y `tc343_auth` (permisos de TrinityCore que la pasarela usa por dentro; sin cuentas):

   ```bash
   mysql -uroot -e "CREATE DATABASE tc343_hotfixes DEFAULT CHARSET utf8mb4; CREATE DATABASE tc343_auth DEFAULT CHARSET utf8mb4; GRANT ALL ON tc343_hotfixes.* TO 'acore'@'localhost'; GRANT ALL ON tc343_auth.* TO 'acore'@'localhost';"
   gunzip -c gateway/sql/tc343_hotfixes.sql.gz | mysql -uroot tc343_hotfixes
   mysql -uroot tc343_auth < gateway/sql/tc343_auth.sql
   ```

3. Si el servidor no está en 127.0.0.1, cambia `address`/`localAddress` del reino 3.4.3 (`acore_auth.realmlist`, id 2).

**4. Datos del servidor: DBC, maps, vmaps y mmaps del 3.3.5**

El servidor de mundo es AzerothCore 3.3.5, así que funciona con los **DBC del 3.3.5** y los datos de mapas, igual
que cualquier servidor AzerothCore:

- `dbc/`: las tablas del juego 3.3.5 (hechizos, objetos, criaturas, zonas...).
- `maps/`: alturas del terreno y agua.
- `vmaps/`: edificios y objetos (línea de visión y colisiones).
- `mmaps/`: mallas de navegación (cómo se mueven las criaturas y los bots).
- `Cameras/`: cámaras de las cinemáticas.

Descarga el `Data.zip` ya preparado de [wowgaming/client-data](https://github.com/wowgaming/client-data/releases)
(la versión que corresponde a AzerothCore) y descomprímelo en la carpeta de datos del worldserver: `DataDir` de
`worldserver.conf`, por defecto una carpeta `data` junto a `worldserver`. Tienen que quedar `data/dbc`,
`data/maps`, `data/vmaps`, `data/mmaps` y `data/Cameras`.

> **¿Por qué dos conjuntos de datos?** El servidor de mundo solo usa los **DBC del 3.3.5** de este paso. La pasarela
> traduce para el cliente 3.4.3, así que necesita saber qué tiene ese cliente: los **DB2 del 3.4.3** del paso 5. La
> lógica del servidor nunca lee DB2 y la pasarela nunca lee los DBC del 3.3.5.

**5. Datos del cliente 3.4.3 para la pasarela (DB2)**

La pasarela necesita los DB2 de tu propio cliente. Copia `mapextractor` (de `build-gateway`) a la carpeta de
instalación de WoW Classic (la que contiene `_classic_`) y ejecuta `mapextractor -e 2` (solo DB2; los mapas no hacen
falta). Crea `dbc/<idioma>/` (p. ej. `dbc/esES/`); mete esa carpeta `dbc` dentro de una carpeta `data343` junto a
`worldgate` (o apunta `DataDir` de `worldgate.conf` a la carpeta que contiene `dbc`).

Vale el cliente en cualquier idioma: la pasarela usa `dbc/enUS` si existe y, si no, el primer idioma que encuentre;
para fijar uno, pon `Locale` en `worldgate.conf` (p. ej. `Locale = esES`). Si no encuentra DB2 se para con un
error que dice dónde los ha buscado.

**6. Configuración**

| Fichero | Qué revisar |
|---|---|
| `worldserver.conf` | lo normal de AzerothCore (`WorldServerPort = 8085`, `RealmID = 1`) y `DataDir`: la carpeta con los datos 3.3.5 (`dbc/`, `maps/`, `vmaps/`, `mmaps/`, `Cameras/` de client-data; por defecto `data` junto al worldserver) |
| `bnetserver.conf` | `LoginDatabaseInfo`; `LoginREST.ExternalAddress`/`LocalAddress` con la IP del servidor |
| `worldgate.conf` | copia de `worldgate.conf.dist`: bases de datos, `DataDir` y `WorldServerConf` (ruta a tu `worldserver.conf`) |

`bnetserver` necesita `bnetserver.cert.pem` y `bnetserver.key.pem` a su lado (son los certificados públicos de
TrinityCore, los que acepta Arctium).

**7. Arrancar y entrar**

1. Arranca MySQL, `worldserver`, `bnetserver` y `worldgate` (en cualquier orden; la pasarela conecta con el
   worldserver cuando entra un jugador).
2. Crea la cuenta en la consola del worldserver: `.account create USUARIO CLAVE` (y `.account set gmlevel` si quieres).
3. En el cliente, `_classic_/WTF/Config.wtf` debe tener `SET portal "127.0.0.1"` (o la IP del servidor).
4. Abre el juego con **Arctium WoW Launcher** desde la carpeta de instalación y entra con `USUARIO` y `CLAVE`.

### Puertos

| Servicio | Puerto |
|---|---|
| bnetserver (Battle.net / REST) | 1119 / 8081 |
| worldgate (cliente 3.4.3) | 8086 y 8087 |
| worldserver (AzerothCore) | 8085 |

### Regenerar los hotfixes

Los generadores están en `gateway/trinitycore-343/src/server/worldgate` (Python 3). Se configuran con variables de
entorno: `WG_MYSQL`, `WG_DB_HOST`, `WG_DB_PORT`, `WG_DB_USER`, `WG_DB_PASS`, `WG_WORLD_DB`, `WG_HOTFIX_DB`, `WG_AC_DATA`
(carpeta de datos del worldserver, la que tiene `dbc`) y `WG_DB2_DIR` (DB2 del cliente 3.4.3).

| Script | Qué genera |
|---|---|
| `gen_hotfix_objetos_ac.py` | objetos del cliente con los valores de `item_template` (ItemSparse/Item/ItemEffect) |
| `gen_hotfix_extendedcost_ac.py` | precios de honor, arena y fichas de los vendedores |
| `valida_hotfix_ac.py fichero.sql` | comprueba un SQL generado contra la base de hotfixes |
| `gen_monturas.py`, `gen_auras_puntos.py` | tablas de la pasarela para los tooltips de monturas y auras |

Si escribes filas de `item_sparse` a mano: el cliente 3.4.3 suma `QualityModifier` al daño (como DPS) y a la
armadura, y sin la marca `0x4000` en `Flags2` no enseña `SellPrice`/`BuyPrice` sino un precio calculado. Para enseñar
valores exactos: `QualityModifier = 0` y `Flags2 |= 0x4000`.

### AIO

`contrib/aio/AIO_Server` va en la carpeta `lua_scripts` del worldserver (Eluna/ALE). `contrib/aio/AIO_Client` es el
addon del cliente: cópialo a `_classic_/Interface/AddOns/AIO_Client` (lleva `AIO_Client_Wrath.toc` para 3.4.3 y la
capa `AIO_Compat343.lua`). Los addons que mandes por AIO tienen que usar la API de 3.4.3: para portar los de 3.3.5,
ver `contrib/addons/Compat335/README.md`.

### Fallos, opiniones y cómo colaborar

Este proyecto es **de código abierto** y está lejos de estar terminado: **habrá** fallos, y justo por eso es público.
Cualquier opinión es bienvenida, y **cualquiera puede corregir cosas y continuarlo**: entre todos podemos mejorarlo.

- **¿Has encontrado un fallo?** Abre una **Issue** con qué hiciste, qué esperabas y qué pasó. Adjunta los logs:
  `worldgate.log` (pasarela), `Server.log`/`Errors.log` (worldserver) y, si el cliente se cerró, el `.txt` de
  `_classic_/Errors`.
- **¿Has arreglado algo?** Los pull requests son bienvenidos. Cambios acotados y explica cómo lo has probado.
- **¿Quieres ayudar?** Coge cualquier issue abierta, o prueba una zona, una mazmorra o una clase y cuenta qué no va.

### Licencias

AzerothCore y TrinityCore: GPL-2.0 (ver `LICENSE` y `gateway/trinitycore-343/COPYING`). Cada módulo y AIO conservan
su licencia en su carpeta. World of Warcraft es una marca de Blizzard Entertainment; este proyecto no incluye ningún
fichero del cliente.
