-- account_access con columnas de TrinityCore (bnetserver) generadas desde las de AzerothCore
-- cada columna se añade solo si no existe (la base puede traer alguna de antes)
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'account_access' AND COLUMN_NAME = 'AccountID');
SET @q := IF(@c = 0, 'ALTER TABLE `account_access` ADD COLUMN `AccountID` int unsigned AS (id) STORED', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'account_access' AND COLUMN_NAME = 'SecurityLevel');
SET @q := IF(@c = 0, 'ALTER TABLE `account_access` ADD COLUMN `SecurityLevel` tinyint unsigned AS (gmlevel) STORED', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;

-- el cliente 3.4.3 manda os = 'Wn64' (4 caracteres); en AzerothCore la columna es varchar(3)
ALTER TABLE account MODIFY os varchar(4) NOT NULL DEFAULT '';
