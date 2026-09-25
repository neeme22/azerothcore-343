-- Columnas que usa esta versión del módulo (SpellRegulator.h lee power_pct) y que faltaban en su SQL base.
ALTER TABLE `spellregulator`
  ADD COLUMN `power_pct` float NOT NULL DEFAULT '100' COMMENT '% del coste de poder (mana/ira/energia/runas). 100 = sin cambios, 50 = mitad, 200 = doble' AFTER `percentage`,
  ADD COLUMN `comment` varchar(128) DEFAULT NULL COMMENT 'Nombre del hechizo (solo referencia)';
ALTER TABLE `npc_spell_amplification`
  ADD COLUMN `comment` varchar(128) DEFAULT NULL;
