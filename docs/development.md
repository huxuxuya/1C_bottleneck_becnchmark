# Проверки и запуск

[Главная](../README.md) · [Команды Make](../Makefile) · [Настройки Docker](../.env.example)

## Как устроен прогон

Исходники конфигурации и расширения тестов лежат в `edt/`. Перед выполнением они экспортируются из EDT в XML-каталоги `build/xml/cf` и `build/xml/cfe/BenchmarkTests`. Затем сценарий создаёт или подключает информационную базу, устанавливает расширение `BenchmarkTests` и YAxUnit, запускает тесты через `/CRunUnitTests` и сохраняет отчёты.

| Команда | Где работает 1С | Тип базы | Для чего подходит |
|---|---|---|---|
| `make test-local` | На машине разработчика | Файловая ИБ `build/ib-local-test` | Быстрый прогон без Docker |
| `make test-ibcmd` | В Docker | Файловая ИБ `build/ib` | Изолированный прогон в контейнере |
| `make test-postgres` | В Docker: клиент, кластер 1С и PostgreSQL | Клиент-серверная ИБ в PostgreSQL | Проверка пути через кластер 1С и СУБД |

Во всех вариантах тестируется конфигурация из XML-экспорта, а не исходники EDT напрямую. После изменения `.bsl`, `.mdo` или `.form` сначала повторите экспорт. Все три варианта запускают YAxUnit пакетно; вручную открывать форму бенчмарка и нажимать кнопку запуска не нужно.

## 1. Экспорт из EDT в XML

Экспорт нужен перед первым прогоном и после изменения проектов EDT.

Локально, если установлена 1C:EDT:

```bash
make export-xml
```

В Docker:

```bash
make build-edt      # один раз, если образ EDT ещё не собран
make export-xml-docker
```

Для Docker-экспорта положите дистрибутив EDT в `docker/dist/edt/` либо используйте образ, в котором доступна `1cedtcli` или `ring`. Для тестов в Docker нужны установленные бинарные файлы платформы 1С: положите необходимые `.deb`-пакеты в `docker/dist/`. Эти дистрибутивы и лицензии в Git не хранятся.

На Apple Silicon с x86_64-бинарниками 1С задайте в `.env`:

```dotenv
DOCKER_PLATFORM=linux/amd64
```

Для локального EDT CLI на macOS можно указать путь явно. Если EDT запущена через Rosetta, передайте отдельный ini с путём к совместимой JVM:

```bash
EDT_CLI="/путь/к/1cedtcli" \
EDT_INI_FILE="/путь/к/1cedt-x86.ini" \
make export-xml
```

## 2. Локальный прогон на файловой ИБ

Требуются локальные `ibcmd` и клиент платформы 1С (`1cv8` или `1cv8c`), доступные через `PATH` или каталог `/opt/1cv8`. Если установлены оба клиента, сценарий предпочитает толстый `1cv8`. Версии `ibcmd` и клиента должны быть совместимы. Для запуска клиенту также нужна доступная лицензия 1С.

```bash
make export-xml
make test-local
```

`make test-local` использует локальные бинарные файлы, Docker не запускается. Команда каждый раз пересоздаёт **только** `build/ib-local-test`; ранее существовавшую `build/ib` она не использует. Она также скачает YAxUnit в `build/deps/YAxUnit.cfe`, если файла ещё нет. Итоговые отчёты находятся в `build/reports/`.

### Запуск в уже подготовленной локальной ИБ без `ibcmd`

Это ручной вариант для файловой базы, в которой уже установлены актуальная конфигурация, расширение `BenchmarkTests` и расширение YAxUnit. Он не обновляет конфигурацию и расширения. Если база ещё не подготовлена, используйте `make test-local` или Docker-вариант.

Сначала создайте локальный файл настроек запуска YAxUnit, чтобы отчёты записывались в `build/reports/`:

```bash
mkdir -p build/reports
sed \
  -e "s|/workspace/build/reports|$PWD/build/reports|g" \
  -e "s|/workspace|$PWD|g" \
  tools/yaxunit.json > build/reports/yaxunit-existing-run.json
```

Затем укажите установленный клиент 1С и путь к готовой файловой ИБ:

```bash
CLIENT_BIN="/путь/к/1cv8"
"$CLIENT_BIN" ENTERPRISE "/F/путь/к/файловой/ИБ" \
  /Out "$PWD/build/reports/platform.log" -NoTruncate \
  /DisableStartupDialogs /DisableStartupMessages \
  "/CRunUnitTests=$PWD/build/reports/yaxunit-existing-run.json"
```

После запуска проверьте `build/reports/exitcode.txt` и `build/reports/junit.xml`. Если на машине нет `ibcmd` и нет подготовленной базы, запустите файловый вариант через Docker (`make test-ibcmd`).

## 3. Файловая ИБ в Docker

Этот вариант использует контейнер с `ibcmd` и клиентом 1С, но сама ИБ остаётся файловой. Docker Compose монтирует репозиторий в контейнер как `/workspace`.

```bash
cp .env.example .env
make export-xml              # или make export-xml-docker
make build-ibcmd             # собрать образ раннера, если он ещё не собран
make test-ibcmd
```

Образ раннера должен содержать платформу 1С, `ibcmd` и клиент. Если `docker/dist/` пуст, базовый образ Ubuntu сам по себе этих компонентов не содержит, и запуск остановится с сообщением об отсутствующей `ibcmd`. После подготовки образа Compose передаёт контейнеру лицензионный volume `1c-licenses`.

**Важно:** сценарий `test-ibcmd` удаляет и заново создаёт каталог `build/ib`. Не храните в нём базу, которую нужно сохранить. Для локального прогона, который не затрагивает этот каталог, используйте `make test-local`.

## 4. PostgreSQL и кластер 1С в Docker

Этот вариант запускает отдельные сервисы PostgreSQL и кластера 1С. Тестовый клиент подключается к ИБ через кластер, а `ibcmd` создаёт ИБ в PostgreSQL. Репозиторий сейчас содержит готовый Compose-сценарий для PostgreSQL; другие СУБД отдельными командами проекта не настроены.

Сначала подготовьте `.env`. `PG_DB` — имя тестовой базы, которую создаст `ibcmd`; контейнер PostgreSQL стартует со служебной базой `postgres`. Не задавайте `PG_DB=postgres`.

```dotenv
DOCKER_PLATFORM=linux/amd64
PG_HOST=postgres
PG_PORT=5432
PG_DB=benchmark_ib
PG_USER=postgres
PG_PASSWORD=postgres
```

Замените тестовый пароль на свой, если это не изолированный локальный стенд. Затем выполните:

```bash
make export-xml                  # или make export-xml-docker
make build-ibcmd                 # образ с ibcmd и клиентом для раннера
make build-postgres              # образ сервера 1С (ragent/rac)
make up-postgres                 # запустить PostgreSQL и кластер 1С
make test-postgres
```

`make test-postgres` создаёт PostgreSQL-базу с именем из `PG_DB`, загружает конфигурацию, регистрирует ИБ в кластере и запускает YAxUnit. Имя ИБ в кластере берётся из `PG_DB`. Для опубликованных портов Compose должны быть свободны `5432`, `1540–1541`, `1545` и диапазон `1560–1591`.

Состояние PostgreSQL и кластера хранится в Docker volumes и переживает `make down`. Повторный `make test-postgres` с тем же `PG_DB` не является сбросом базы: `ibcmd --create-database` ожидает, что такой базы ещё нет. Для следующего независимого прогона задайте новое имя `PG_DB` либо сбросьте весь одноразовый стенд:

```bash
docker compose -f docker/docker-compose.yml --profile postgres down -v
```

Команда с `-v` удаляет данные PostgreSQL, состояние кластера и сохранённый лицензионный volume. Используйте её только для тестового стенда, данные которого можно удалить. Обычный `make down` останавливает Compose-сервисы, но сохраняет volumes. Флаг `--create-database` создаёт базу только при её отсутствии; повторное создание существующей базы завершается ошибкой. Это поведение описано в [официальном руководстве администратора 1С](https://1c-dn.com/library/tutorials/1c_enterprise_administrator_guide_file_mode_8_3_27/).

Для контейнеров нужна активная лицензия платформы 1С. Кластер и раннер используют volume `1c-licenses`, поэтому `make down` сохраняет лицензию между запусками, а `down -v` удаляет и её.

## Результаты и диагностика

После запуска проверяйте:

- `build/reports/junit.xml` — результаты тестов в JUnit;
- `build/reports/exitcode.txt` — код завершения YAxUnit (`0` означает успешный прогон);
- `build/reports/yaxunit.log` — журнал запуска, список тестов и текст ошибок;
- `build/reports/platform.log` — сообщения платформы из `/Out`, включая маркер фактических значений CPU-пробы.

Дополнительные Python-проверки связей формы запускаются отдельно:

```bash
python3 -m unittest discover -s tools/tests -p 'test_*.py' -v
```

GitHub Actions запускает проверку EDT/XML/JSON и эти Python-тесты на обычном runner. Они не компилируют BSL. YAxUnit назначен на self-hosted runner с метками `linux` и `1c`; если такой runner не подключён, job остаётся в очереди. Для проверки встроенного языка используйте локальный прогон или Docker-вариант, который действительно запускает платформу 1С.

## Разработка в 1C:EDT

1. Откройте 1C:EDT и выполните **File → Import… → General → Existing Projects into Workspace**.
2. Выберите корень репозитория или каталог `edt/` — EDT подхватит оба проекта:
   - `benchmark` — основная конфигурация (`V8ConfigurationNature`);
   - `BenchmarkTests` — расширение тестов YAxUnit (`V8ExtensionNature`).
3. Служебные каталоги `.settings/`, `.build/`, `.metamodel/` исключены в `.gitignore`; в Git хранятся `.project`, `DT-INF/PROJECT.PMF` и исходники `.mdo`, `.bsl`, `.form`.

## Нативная компонента CPU в Docker

Код Native API измерителя расположен в `native/cpu-frequency/`. Контейнер сборки содержит CMake и кросс-компиляторы для Linux x86/x86_64/ARM64 и Windows x86/x86_64. Сам SDK 1С контейнер и репозиторий не содержат: каталог `include` из официального архива VNCOMPS монтируется в контейнер только для чтения. Для macOS x86_64 и ARM64 нужна сборка на macOS с Xcode SDK. Частотный алгоритм и CLI можно собрать без SDK.

```bash
make native-core
```

Команда создаёт `build/native/core/` и запускает сборку внутри Docker. Для Native API задайте путь к официальным заголовкам SDK:

```bash
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMPS/include" make native-linux-arm64
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMPS/include" make native-linux-x86_64
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMPS/include" make native-linux-i386
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMPS/include" make native-windows-x86_64
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMPS/include" make native-windows-i386
```

Для macOS x86_64 используйте Xcode SDK на macOS-хосте:

```bash
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMPS/include" make native-macos-x86_64
NATIVEAPI_INCLUDE_DIR="/путь/к/VNCOMPS/include" make native-macos-arm64
```

Эльбрус требует совместимый E2K компилятор и sysroot; в общий Docker-образ они не включены. Windows cross-build проверяет компоновку, но итоговый DLL нужно запускать в соответствующей Windows 1С. Для Linux цель x86_64 можно собрать кросс-компилятором в контейнере; выполнение и замеры нужно подтверждать на Linux x86_64. Сборка для платформ, которые пользователь не предоставил как runner, не считается runtime-проверкой.

Соберите нужные архитектуры, затем упакуйте только уже существующие бинарные файлы в ZIP с `manifest.xml`:

```bash
make native-package NATIVE_TARGETS="linux-arm64 linux-i386 linux-x86_64 windows-i386 windows-x86_64 macos-x86_64 macos-arm64"
```

Команда создаёт `build/native/CpuFrequencyProbe.zip`; проверенная сборка 0.1.0 также сохранена в [`releases/CpuFrequencyProbe-0.1.0.zip`](../releases/CpuFrequencyProbe-0.1.0.zip), её SHA-256 — рядом. Подробно о составе результата, методе оценки и вызове из BSL: [Нативный замер CPU](native-cpu-probe.md).
