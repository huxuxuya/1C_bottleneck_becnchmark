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

## Примечание по лицензиям

Для запуска `1cv8c` / `ragent` в контейнере нужна активная лицензия (например, бесплатная комьюнити-лицензия разработчика 1С). Каталог лицензий монтируется в Docker volume `1c-licenses` (`/var/1C/licenses`), чтобы активация сохранялась между перезапусками контейнеров.
