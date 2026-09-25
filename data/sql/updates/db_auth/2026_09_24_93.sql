-- Reino para el cliente WoW Classic 3.4.3 (54261): apunta a la pasarela worldgate (puerto 8086), no al worldserver.
-- El id tiene que coincidir con RealmId de worldgate.conf.
DELETE FROM `realmlist` WHERE `id` = 2;
INSERT INTO `realmlist` (`id`, `name`, `address`, `localAddress`, `localSubnetMask`, `port`, `icon`, `flag`, `timezone`, `allowedSecurityLevel`, `population`, `gamebuild`) VALUES
(2, 'AzerothCore 3.4.3', '127.0.0.1', '127.0.0.1', '255.255.255.0', 8086, 1, 0, 1, 0, 0, 54261);
