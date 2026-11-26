# Нативный замер CPU через Native API 1С

[Главная](../README.md) · [Сборка и запуск](development.md) · [Проба 001](tests/cpu/001.md)

## Назначение

`CpuFrequencyProbe` — внешняя компонента 1С на C++ для однопоточной зависимой целочисленной нагрузки. Она переносит принцип пробы частоты 7-Zip в процесс платформы 1С: выполняет известное число пар `add`/`xor`, измеряет монотонное время и вычисляет скорость учтённых операций. Код не запускает shell, системные команды, внешние утилиты или отдельный агент.

Это оценка частоты по скорости операций, а не чтение аппаратного регистра частоты. Результат зависит от архитектуры, компилятора, режима энергопотребления, фоновой нагрузки и виртуализации. Он нужен для сопоставления одинакового нативного алгоритма и для сравнения с BSL-пробами 001/026; число нельзя трактовать как гарантированную текущую физическую частоту ядра.

В отличие от двух реализаций BSL, нативный измеритель не включает стоимость виртуальной машины встроенного языка и BSL-цикла. Вызов из BSL всё равно включает небольшие накладные расходы до и после измеренного участка, но они не входят в `elapsedNs`.

Компонента следует Native API: объект реализует `IComponentBase`, а библиотека экспортирует четыре функции загрузки. ABI зависит от ОС и разрядности, поэтому собираются отдельные варианты; строки API преобразуются в двухбайтовый `WCHAR_T` независимо от системного размера `wchar_t`. См. [официальное описание Native API](https://1c-dn.com/library/add_in_creation_technology_creating_add_ins_using_native_api_technology/) и [рекомендации по переносимости](https://1c-dn.com/library/add_in_creation_technology_specifics_of_add-in_development_using_native_api/).

## Формула и устойчивость

Один блок повторяет 64 зависимые пары `sum += value; sum ^= value`, то есть компонент считает 128 целочисленных команд. Для каждого замера:

```text
operationsPerSample = blocksPerSample × 128
operationsPerSecond = operationsPerSample / medianElapsedSeconds
estimatedMHz = operationsPerSecond / 1 000 000
```

Эквивалентная формула для времени в наносекундах: `estimatedMHz = operationsPerSample × 1000 / medianElapsedNs`. Это соглашение об оценке, такое же по смыслу, как в 7-Zip: скорость известных команд выражается в условных MHz. Оно не учитывает, что разные CPU выполняют команды с разным числом тактов, могут исполнять их параллельно, менять частоту и по-разному оптимизировать код. Исходный алгоритм 7-Zip находится в [`Bench.cpp`](https://github.com/ip7z/7zip/blob/main/CPP/7zip/UI/Common/Bench.cpp).

По умолчанию выполняются 2 прогрева и 7 измеряемых серий по 1 000 000 блоков. В ответе остаются длительности каждой серии, минимум, медиана, максимум и контрольная сумма каждой измеряемой серии. Чтобы синхронный вызов не зависал из-за чрезмерного объёма, общее число блоков с учётом прогревов ограничено 15 000 000 за вызов; число прогревов — 0–20, число измерений — 3–31. Релизная оптимизация компилятора обязательна для сопоставимых результатов.

## Интерфейс компоненты

- Имя расширения: `CpuBenchmark`.
- ProgID объекта: `AddIn.CpuBenchmark.CpuProbe`.
- Функция: `Measure(BlocksPerSample, Warmups, Samples)`.
- Возвращаемое значение: JSON-строка в UTF-8.

Пример вызова после добавления ZIP к макету внешней компоненты:

```bsl
ПодключитьВнешнююКомпоненту(
    "ОбщийМакет.CpuFrequencyProbe",
    "CpuBenchmark",
    ТипВнешнейКомпоненты.Native);

ИзмерительCPU = Новый("AddIn.CpuBenchmark.CpuProbe");
JSONРезультата = ИзмерительCPU.Measure(1000000, 2, 7);
```

Вызов должен выполняться там, где нужно измерить CPU: на клиенте — в клиентском процессе, на сервере — в процессе 1С. Для тонкого клиента, веб-клиента и серверной ИБ нужно отдельно включить ZIP в конфигурацию, выполнить загрузку для нужного контекста и проверить поддержку Native API и конкретной архитектуры платформы. Синтаксис подключения описан в [документации 1С по Native API](https://1c-dn.com/library/add_in_creation_technology_creating_add_ins_using_native_api_technology/) и [подключению внешней компоненты](https://its.1c.ru/db/content/intgr83/src/162.html).

Пример структуры результата:

```json
{
  "schemaVersion": 1,
  "componentVersion": "0.1.0",
  "algorithm": "dependent_u32_add_xor_7zip_style_v1",
  "architecture": "arm64",
  "compiler": "clang ...",
  "blocksPerSample": 1000000,
  "warmups": 2,
  "sampleCount": 7,
  "countedOperationsPerBlock": 128,
  "countedOperationsPerSample": 128000000,
  "sampleElapsedNs": [ ... ],
  "sampleChecksums": [ ... ],
  "minElapsedNs": 0,
  "medianElapsedNs": 0,
  "maxElapsedNs": 0,
  "operationsPerSecond": 0,
  "estimatedMHz": 0,
  "checksum": 0,
  "physicalClockRead": false,
  "interpretation": "..."
}
```

Нули в примере — поля-схема, не измеренный результат. Реальные значения записываются только после вызова функции.

## Сборка

Нужен официальный комплект разработки внешних компонент 1С: каталог `VNCOMP83/include` с `ComponentBase.h`, `AddInDefBase.h`, `IMemoryManager.h` и зависимыми заголовками. Заголовки не скопированы в Git. На этой машине такой каталог пока не найден, поэтому Native API библиотека и ZIP для 1С ещё не собраны; C++ ядро и CLI можно собрать без SDK.

```bash
make native-core
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMP83/include" make native-linux-arm64
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMP83/include" make native-linux-x86_64
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMP83/include" make native-linux-i386
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMP83/include" make native-windows-x86_64
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMP83/include" make native-windows-i386
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMP83/include" make native-macos-x86_64
```

Пути сборки:

- Linux ARM64, x86 и x86_64 и Windows x86/x86_64 — контейнер `docker/native-builder/Dockerfile` с кросс-компиляторами.
- macOS x86_64 — локальная сборка на macOS с Xcode SDK; Linux Docker не может линковать macOS библиотеку.
- Linux E2K — отдельный runner с компилятором и sysroot Эльбруса; он не входит в стандартный образ.

Переносимое ядро и CLI собраны на macOS ARM64 и в Linux ARM64 Docker. Статическое ядро также успешно кросс-компилировано в Docker для Linux i386/x86_64/ARM64 и Windows i386/x86_64, а на macOS — для x86_64. Это подтверждает переносимость C++ ядра, но не Native API ABI, загрузку DLL/so/dylib в 1С или результаты замера на этих системах.

После сборки можно упаковать доступные архитектуры:

```bash
make native-package NATIVE_TARGETS="linux-arm64 linux-x86_64 windows-x86_64 macos-x86_64"
```

Скрипт добавляет только запрошенные бинарные файлы и создаёт `MANIFEST.XML` в ZIP `build/native/CpuFrequencyProbe.zip` по [формату 1С](https://1c-dn.com/library/add_in_creation_technology_manifest_xml_file_description/). Он не подменяет runtime-проверку: Linux ARM64 нужно запускать на Linux ARM64, Windows DLL — в Windows 1С соответствующей разрядности, macOS-библиотеку — в поддерживаемой macOS-платформе.

## Состояние интеграции

В репозитории есть исходный код алгоритма, Native API-адаптера, Docker-среда, CMake toolchain-файлы и упаковщик ZIP. На текущем хосте отсутствует официальный Native API SDK, поэтому адаптер нельзя пока скомпилировать и проверить загрузкой в 1С. Нативная оценка ещё не включена в сценарий полного бенчмарка и реестр результатов; текущие результаты 001 и 026 остаются BSL-измерениями.
