"""Static regression checks for the benchmark form's start command wiring."""

from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
FORM_PATH = ROOT / "edt/benchmark/src/DataProcessors/ЗапускБенчмарка/Forms/Форма/Form.form"
MODULE_PATH = ROOT / "edt/benchmark/src/DataProcessors/ЗапускБенчмарка/Forms/Форма/Module.bsl"
YAXUNIT_PATH = (
    ROOT
    / "edt/BenchmarkTests/src/CommonModules/Тест_ФормаЗапускБенчмарка_ОткрытиеИЗапуск/Module.bsl"
)


def child_text(element: ET.Element, child_name: str) -> str | None:
    for child in element:
        if child.tag.rsplit("}", 1)[-1] == child_name:
            return child.text
    return None


class BenchmarkFormCommandTests(unittest.TestCase):
    def test_diagnosis_and_checks_are_separate_default_pages(self) -> None:
        root = ET.parse(FORM_PATH).getroot()
        pages = [
            element
            for element in root.findall("items")
            if child_text(element, "name") == "ВкладкиПульта"
        ]
        self.assertEqual(len(pages), 1, "the form must have one pages container")
        self.assertEqual(child_text(pages[0], "type"), "Pages")

        page_items = pages[0].findall("items")
        self.assertEqual(
            [child_text(page, "name") for page in page_items],
            ["СтраницаДиагноз", "СтраницаПроверки"],
            "the diagnosis page must be the default first page",
        )

        diagnosis_names = {
            child_text(element, "name")
            for element in page_items[0].iter()
            if child_text(element, "name")
        }
        checks_names = {
            child_text(element, "name")
            for element in page_items[1].iter()
            if child_text(element, "name")
        }
        self.assertIn("КнопкаВыполнитьПрогон", diagnosis_names)
        self.assertIn("ГлавноеУзкоеМесто", diagnosis_names)
        self.assertIn("ОбщийБалл", diagnosis_names)
        self.assertNotIn("ТаблицаРезультатов", diagnosis_names)
        self.assertIn("ТаблицаРезультатов", checks_names)
        self.assertIn("ТочкиПроб042", checks_names)
        self.assertIn("ГруппаДополнительныеПроверки", checks_names)
        self.assertIn("ТаблицаПокрытия", diagnosis_names)
        self.assertIn("ГруппаФильтраРезультатов", checks_names)

        attributes = {
            child_text(element, "name"): element
            for element in root.findall("attributes")
            if child_text(element, "name")
        }
        self.assertIn("ПокрытиеПоЗвеньям", attributes)
        result_columns = {
            child_text(column, "name")
            for column in attributes["Результаты"].findall("columns")
        }
        self.assertTrue({"Звено", "Ресурс", "ТипСвидетельства"} <= result_columns)

    def test_start_button_is_bound_to_exported_handler(self) -> None:
        root = ET.parse(FORM_PATH).getroot()
        buttons = [
            element
            for element in root.iter()
            if element.tag.rsplit("}", 1)[-1] == "items"
            and child_text(element, "name") == "КнопкаВыполнитьПрогон"
        ]
        self.assertEqual(len(buttons), 1, "the form must have exactly one start button")
        self.assertEqual(
            child_text(buttons[0], "commandName"),
            "Form.Command.ВыполнитьПрогон",
            "the start button must target the declared form command",
        )

        commands = [
            element
            for element in root.iter()
            if element.tag.rsplit("}", 1)[-1] == "formCommands"
            and child_text(element, "name") == "ВыполнитьПрогон"
        ]
        self.assertEqual(len(commands), 1, "the start form command must be declared exactly once")
        handlers = [
            element
            for element in commands[0].iter()
            if element.tag.rsplit("}", 1)[-1] == "handler"
        ]
        self.assertEqual(len(handlers), 1, "the form command must have exactly one handler")
        self.assertEqual(
            child_text(handlers[0], "name"),
            "ВыполнитьПрогон",
            "the form command must name its BSL handler",
        )

        module = MODULE_PATH.read_text(encoding="utf-8")
        self.assertRegex(
            module,
            r"Процедура\s+ВыполнитьПрогон\s*\(Команда\)\s+Экспорт(?s:.*?)ВыполнитьПрогонНаСервере\(\);",
            "the exported command handler must invoke the server-side benchmark",
        )

    def test_yaxunit_form_scenario_checks_run_results(self) -> None:
        source = YAXUNIT_PATH.read_text(encoding="utf-8")
        self.assertIn("Форма.ВыполнитьПрогон(Неопределено);", source)
        self.assertIn("Форма.ОбщийБалл", source)
        self.assertIn("Форма.Результаты.Количество()", source)

    def test_file_probe_button_is_bound_to_server_runner(self) -> None:
        root = ET.parse(FORM_PATH).getroot()
        buttons = [
            element
            for element in root.iter()
            if element.tag.rsplit("}", 1)[-1] == "items"
            and child_text(element, "name") == "КнопкаВыполнитьПробу042"
        ]
        self.assertEqual(len(buttons), 1, "the form must have exactly one 042 button")
        self.assertEqual(
            child_text(buttons[0], "commandName"),
            "Form.Command.ВыполнитьПробу042",
        )

        commands = [
            element
            for element in root.iter()
            if element.tag.rsplit("}", 1)[-1] == "formCommands"
            and child_text(element, "name") == "ВыполнитьПробу042"
        ]
        self.assertEqual(len(commands), 1, "the 042 form command must be declared once")
        handlers = [
            element
            for element in commands[0].iter()
            if element.tag.rsplit("}", 1)[-1] == "handler"
        ]
        self.assertEqual(len(handlers), 1)
        self.assertEqual(child_text(handlers[0], "name"), "ВыполнитьПробу042")

        module = MODULE_PATH.read_text(encoding="utf-8")
        self.assertRegex(
            module,
            r"Процедура\s+ВыполнитьПробу042\s*\(Команда\)\s+Экспорт(?s:.*?)"
            r"ВыполнитьПробу042НаСервере\(\);",
        )
        self.assertIn(
            "БенчмаркПроба042Хранилище.ВыполнитьПрогон()",
            module,
        )

    def test_layer_resource_projection_and_filter_are_wired(self) -> None:
        root = ET.parse(FORM_PATH).getroot()
        commands = [
            element
            for element in root.iter()
            if element.tag.rsplit("}", 1)[-1] == "formCommands"
            and child_text(element, "name") == "ПрименитьФильтрРезультатов"
        ]
        self.assertEqual(len(commands), 1)
        self.assertIn(
            "ПрименитьФильтрРезультатов",
            [child_text(element, "name") for element in commands[0].iter()],
        )

        module = MODULE_PATH.read_text(encoding="utf-8")
        self.assertIn("КлассифицироватьРезультат", module)
        self.assertIn("ОбновитьПокрытиеНаСервере", module)
        self.assertIn("Элементы.ТаблицаРезультатов.ОтборСтрок", module)
        self.assertIn('Новый ФиксированнаяСтруктура(ОтборСтрок)', module)
        self.assertIn('"Сервер 1С ↔ СУБД"', module)
        self.assertIn('"Запросы и чтение"', module)


if __name__ == "__main__":
    unittest.main()
