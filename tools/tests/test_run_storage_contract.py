"""Static contract checks for normalized run storage and client/server probe 027."""

from pathlib import Path
import xml.etree.ElementTree as ET
import unittest


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "edt/benchmark/src"
CONFIG_PATH = SRC / "Configuration/Configuration.mdo"
STORE_PATH = SRC / "CommonModules/БенчмаркХранилищеПрогонов/Module.bsl"
FORM_PATH = SRC / "DataProcessors/ЗапускБенчмарка/Forms/Форма/Module.bsl"
DIAGNOSTICS_PATH = SRC / "CommonModules/БенчмаркДиагностикиBSL/Module.bsl"
YAXUNIT_CONFIG_PATH = ROOT / "edt/BenchmarkTests/src/Configuration/Configuration.mdo"
YAXUNIT_STORAGE_TEST_PATH = (
    ROOT
    / "edt/BenchmarkTests/src/CommonModules/Тест_ХранилищеПрогонов_Контракт/Module.bsl"
)


class RunStorageContractTests(unittest.TestCase):
    def test_normalized_metadata_is_registered_and_uuids_are_unique(self) -> None:
        config = ET.parse(CONFIG_PATH).getroot()
        objects = {element.text for element in config if element.text}
        self.assertIn("Document.ПрогонИспытаний", objects)
        self.assertIn("InformationRegister.ПопыткиПроб", objects)
        self.assertIn("InformationRegister.РядыИзмерений", objects)
        self.assertIn("InformationRegister.ПоказателиПопыток", objects)
        self.assertIn("CommonModule.БенчмаркХранилищеПрогонов", objects)

        seen: set[str] = set()
        duplicates: set[str] = set()
        for metadata_file in (ROOT / "edt").rglob("*.mdo"):
            metadata = ET.parse(metadata_file).getroot()
            for element in metadata.iter():
                identifier = element.attrib.get("uuid")
                if identifier is None:
                    continue
                if identifier in seen:
                    duplicates.add(identifier)
                seen.add(identifier)
        self.assertFalse(duplicates, f"duplicate metadata UUIDs: {sorted(duplicates)}")

    def test_storage_keeps_aggregates_out_of_attempt_rows(self) -> None:
        source = STORE_PATH.read_text(encoding="utf-8")
        self.assertIn("РегистрыСведений.ПопыткиПроб.СоздатьМенеджерЗаписи()", source)
        self.assertIn("РегистрыСведений.РядыИзмерений.СоздатьМенеджерЗаписи()", source)
        self.assertIn("РегистрыСведений.ПоказателиПопыток.СоздатьМенеджерЗаписи()", source)
        self.assertIn('Статистика = "медиана" Или Статистика = "фиксированное"', source)
        self.assertIn('ИдентификаторРяда + ".aggregate"', source)
        self.assertIn('Показатель.Наличие = СтатусНаличия;', source)

    def test_027_saves_independent_series_and_invalidates_zero_intervals(self) -> None:
        source = FORM_PATH.read_text(encoding="utf-8")
        self.assertIn('Результат.Вставить("Наблюдения", Наблюдения);', source)
        self.assertIn('"net.small_rtt.series_mean_ns"', source)
        self.assertIn('"1c.empty_handler.series_mean_ns"', source)
        self.assertIn('"net.small_rtt.median_ns"', source)
        self.assertIn('"1c.empty_handler.median_ns"', source)
        self.assertIn('"Недостаточно разрешения таймера"', source)
        self.assertIn("СохранитьРезультат027НаСервере(ИдентификаторПрогона, Результат027);", source)

    def test_bsl_proxy_diagnostics_are_context_only(self) -> None:
        source = DIAGNOSTICS_PATH.read_text(encoding="utf-8")
        self.assertIn('Результат.Статус = "BSL-прокси", "Косвенный сигнал"', source)

    def test_yaxunit_storage_contract_test_is_registered(self) -> None:
        config = ET.parse(YAXUNIT_CONFIG_PATH).getroot()
        modules = {element.text for element in config if element.text}
        self.assertIn("CommonModule.Тест_ХранилищеПрогонов_Контракт", modules)
        source = YAXUNIT_STORAGE_TEST_PATH.read_text(encoding="utf-8")
        self.assertIn('ДобавитьСерверныйТест("Тест_ХранилищеПрогонов_Контракт")', source)
        self.assertIn('"server.test.aggregate", 0, "медиана"', source)


if __name__ == "__main__":
    unittest.main()
