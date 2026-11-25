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


if __name__ == "__main__":
    unittest.main()
