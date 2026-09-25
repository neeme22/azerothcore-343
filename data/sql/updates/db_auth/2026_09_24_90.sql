-- Esquema de cuentas para el bnetserver del cliente 3.4.3 (Battle.net)
-- Tablas nuevas tomadas de TrinityCore TDB343.24081; columnas nuevas sin quitar ninguna de AzerothCore.

CREATE TABLE IF NOT EXISTS `battlenet_accounts` (
  `id` int unsigned NOT NULL AUTO_INCREMENT COMMENT 'Identifier',
  `email` varchar(320) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  `srp_version` tinyint NOT NULL DEFAULT '1',
  `salt` binary(32) NOT NULL,
  `verifier` blob NOT NULL,
  `joindate` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_ip` varchar(15) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '127.0.0.1',
  `failed_logins` int unsigned NOT NULL DEFAULT '0',
  `locked` tinyint unsigned NOT NULL DEFAULT '0',
  `lock_country` varchar(2) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '00',
  `last_login` timestamp NULL DEFAULT NULL,
  `online` tinyint unsigned NOT NULL DEFAULT '0',
  `locale` tinyint unsigned NOT NULL DEFAULT '0',
  `os` varchar(4) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '',
  `LastCharacterUndelete` int unsigned NOT NULL DEFAULT '0',
  `LoginTicket` varchar(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT NULL,
  `LoginTicketExpiry` int unsigned DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Account System';

CREATE TABLE IF NOT EXISTS `battlenet_account_bans` (
  `id` int unsigned NOT NULL DEFAULT '0' COMMENT 'Account id',
  `bandate` int unsigned NOT NULL DEFAULT '0',
  `unbandate` int unsigned NOT NULL DEFAULT '0',
  `bannedby` varchar(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  `banreason` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  PRIMARY KEY (`id`,`bandate`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Ban List';

CREATE TABLE IF NOT EXISTS `account_last_played_character` (
  `accountId` int unsigned NOT NULL,
  `region` tinyint unsigned NOT NULL,
  `battlegroup` tinyint unsigned NOT NULL,
  `realmId` int unsigned DEFAULT NULL,
  `characterName` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT NULL,
  `characterGUID` bigint unsigned DEFAULT NULL,
  `lastPlayedTime` int unsigned DEFAULT NULL,
  PRIMARY KEY (`accountId`,`region`,`battlegroup`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- cada columna se añade solo si no existe (la base puede traer alguna de antes)
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'account' AND COLUMN_NAME = 'session_key_bnet');
SET @q := IF(@c = 0, 'ALTER TABLE `account` ADD COLUMN `session_key_bnet` varbinary(64) DEFAULT NULL', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'account' AND COLUMN_NAME = 'timezone_offset');
SET @q := IF(@c = 0, 'ALTER TABLE `account` ADD COLUMN `timezone_offset` smallint NOT NULL DEFAULT ''0''', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'account' AND COLUMN_NAME = 'battlenet_account');
SET @q := IF(@c = 0, 'ALTER TABLE `account` ADD COLUMN `battlenet_account` int unsigned DEFAULT NULL', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'account' AND COLUMN_NAME = 'battlenet_index');
SET @q := IF(@c = 0, 'ALTER TABLE `account` ADD COLUMN `battlenet_index` tinyint unsigned DEFAULT NULL', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'realmlist' AND COLUMN_NAME = 'Region');
SET @q := IF(@c = 0, 'ALTER TABLE `realmlist` ADD COLUMN `Region` tinyint unsigned NOT NULL DEFAULT ''1''', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'realmlist' AND COLUMN_NAME = 'Battlegroup');
SET @q := IF(@c = 0, 'ALTER TABLE `realmlist` ADD COLUMN `Battlegroup` tinyint unsigned NOT NULL DEFAULT ''1''', 'DO 0');
PREPARE st FROM @q; EXECUTE st; DEALLOCATE PREPARE st;

-- semillas de autenticacion de los builds 3.4.3 (TrinityCore TDB343.24081)
DELETE FROM `build_info` WHERE `build` IN (51943,52237,53622,53788,54261);
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`,`winAuthSeed`,`win64AuthSeed`,`mac64AuthSeed`,`winChecksumSeed`,`macChecksumSeed`) VALUES
(51943,3,4,3,NULL,NULL,'926D8C2514A3FEBA84F0DEB031AE41CE',NULL,NULL,NULL),
(52237,3,4,3,NULL,NULL,'3BA993D54FD86EE03E6F81C8FBCE26B7',NULL,NULL,NULL),
(53622,3,4,3,NULL,NULL,'CCC0A843915F46992BB1A650B18CFD67',NULL,NULL,NULL),
(53788,3,4,3,NULL,NULL,'E8F11A6A011B4ED7C20D055221DBCF8F',NULL,NULL,NULL),
(54261,3,4,3,NULL,NULL,'25FD812475DCF26F9F1383AED37FC99E',NULL,NULL,NULL);
-- el reino 3.4.3 (el authserver 3.3.5 filtra por su propio build; este reino es para el cliente nuevo)
-- UPDATE `realmlist` SET `gamebuild`=54261 WHERE `id`=<id del reino 3.4.3>;
