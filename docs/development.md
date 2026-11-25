# Разработка и запуск

[Главная](../README.md) · [Команды Make](../Makefile) · [Настройки окружения](../.env.example)

## Разработка в 1C:EDT

1. Откройте 1C:EDT и выполните **File → Import… → General → Existing Projects into Workspace**.
2. Выберите корневую папку репозитория (или каталог `edt/`) — EDT подхватит оба проекта:
   - `benchmark` (основная конфигурация, `V8ConfigurationNature`)
   - `BenchmarkTests` (расширение тестов YAxUnit, `V8ExtensionNature`)
3. Служебные каталоги `.settings/`, `.build/`, `.metamodel/` исключены в `.gitignore`, в Git коммитятся только `.project`, `DT-INF/PROJECT.PMF` и `src/` (`.mdo`, `.bsl`, `.form`).

## Сборка и запуск в Docker

Пайплайн состоит из двух шагов: **1) экспорт `edt/ → build/xml/`** и **2) загрузка в ИБ через `ibcmd` + прогон YAxUnit**. Команды выполняются из корня репозитория.

1. Скопируйте `.env.example` в `.env` и укажите ваши образы (или положите дистрибутивы в `docker/dist/`):
   ```bash
   cp .env.example .env
   ```
   > На Apple Silicon (`arm64`) с x86_64-бинарниками 1С оставьте `DOCKER_PLATFORM=linux/amd64`.

2. Конвертируйте проекты EDT в XML (локальной `1cedtcli`/`ring` или в Docker):
   ```bash
   make export-xml          # если 1C:EDT установлена локально
   # или
   make export-xml-docker   # через профиль edt-export в Docker
   ```

   На macOS скрипт также ищет `1cedtcli` в пользовательской установке EDT. На Apple Silicon для x86_64 EDT может понадобиться отдельная копия `1cedt.ini` с параметром `-vm` и путём к x86_64 `libjvm.dylib`, вставленными перед `-vmargs`. Передайте её через `EDT_INI_FILE`; оригинальный файл установки менять не нужно:
   ```bash
   EDT_CLI="/путь/к/1cedt.app/Contents/Eclipse/1cedtcli" \
   EDT_INI_FILE="/путь/к/1cedt-x86.ini" make export-xml
   ```

3. **Вариант 1 (`ibcmd` — файловая ИБ и быстрые тесты):**
   ```bash
   make test-ibcmd
   ```
   JUnit-отчёт появится в `build/reports/junit.xml`.

4. **Вариант 2 (`postgres` — PostgreSQL + кластер 1С):**
   ```bash
   make build-ibcmd         # если локального образа раннера ещё нет
   make up-postgres
   make test-postgres
   ```

`make test-ibcmd` и `make test-postgres` запускают YAxUnit пакетно через клиент 1С с `/CRunUnitTests`; вручную открывать форму не требуется. Для локального запуска образ раннера должен содержать `ibcmd`, `1cv8c` и подходящие пакеты платформы 1С. В репозитории эти дистрибутивы не хранятся; пустой `docker/dist/` сам по себе не даёт образ с платформой.

В GitHub Actions XML/JSON-проверка и тест Python работают на обычном GitHub runner. Job YAxUnit назначен на self-hosted runner с метками `linux` и `1c`; если такой runner не подключён или недоступен, job остаётся в очереди и результата YAxUnit нет.

## Примечание по лицензиям

Для запуска `1cv8c` / `ragent` в контейнере нужна активная лицензия (например, бесплатная комьюнити-лицензия разработчика 1С). Каталог лицензий монтируется в Docker volume `1c-licenses` (`/var/1C/licenses`), чтобы активация сохранялась между перезапусками контейнеров.
