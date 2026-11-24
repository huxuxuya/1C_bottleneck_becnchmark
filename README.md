# 1C Bottleneck Benchmark

Конфигурация-бенчмарк **1С:Предприятие** для воспроизводимого исследования узких мест (блокировки, планы запросов PostgreSQL/SDBL, конкурентная нагрузка) с разработкой в **1C:EDT**, сборкой и запуском в Docker и модульным тестированием на **YAxUnit**.

## Структура репозитория

```text
├── .github/workflows/ci.yml          # CI-пайплайн (валидация EDT/JSON + экспорт + YAxUnit)
├── benchmarks/                       # Профили и сценарии нагрузочных замеров
│   └── profiles/default.json
├── docker/
│   ├── 1c-edt/Dockerfile             # Образ 1C:EDT CLI для экспорта edt/ -> build/xml/
│   ├── 1c-ibcmd/Dockerfile           # Образ для профиля ibcmd (ibcmd + 1cv8c + Xvfb)
│   ├── 1c-server/Dockerfile          # Образ кластера 1С (ragent + ras + logcfg.xml)
│   ├── dist/                         # Локальные установщики 1С и EDT (не коммитятся)
│   ├── techlog/logcfg.xml            # Конфигурация технологического журнала 1С
│   └── docker-compose.yml            # Профили: edt-export, ibcmd, postgres
├── edt/                              # Исходный код в формате 1C:EDT (единственный источник правды)
│   ├── benchmark/                    # Основная конфигурация-бенчмарк (.project, DT-INF, src/)
│   └── BenchmarkTests/               # Проект расширения с юнит-тестами YAxUnit
├── tools/
│   ├── scripts/
│   │   ├── export-edt-to-xml.sh      # Конвертация edt/ -> build/xml/ через 1cedtcli или ring
│   │   ├── build-and-test.sh         # Сборка ИБ из build/xml/ и прогон YAxUnit (--mode ibcmd|postgres)
│   │   └── download-yaxunit.sh       # Загрузка YAxUnit.cfe из релизов GitHub
│   ├── autumn-properties.json        # Конфиг vanessa-runner 3.x (при использовании vrunner)
│   └── yaxunit.json                  # Настройки прогона YAxUnit и JUnit-отчёта
├── .bsl-language-server.json         # Настройки BSL Language Server (edt/benchmark/src)
├── .env.example                      # Шаблон переменных окружения
├── Makefile                          # Команды сборки, экспорта и тестирования
└── packagedef                        # Дескриптор OneScript-проекта
```

## Разработка в 1C:EDT

1. Откройте 1C:EDT и выполните **File → Import… → General → Existing Projects into Workspace**.
2. Выберите корневую папку репозитория (или каталог `edt/`) — EDT подхватит оба проекта:
   - `benchmark` (основная конфигурация, `V8ConfigurationNature`)
   - `BenchmarkTests` (расширение тестов YAxUnit, `V8ExtensionNature`)
3. Служебные каталоги `.settings/`, `.build/`, `.metamodel/` исключены в `.gitignore`, в Git коммитятся только `.project`, `DT-INF/PROJECT.PMF` и `src/` (`.mdo`, `.bsl`, `.form`).

## Сборка и запуск в Docker

Пайплайн состоит из двух шагов: **1) экспорт `edt/ → build/xml/`** и **2) загрузка в ИБ через `ibcmd` + прогон YAxUnit**.

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
   make up-postgres
   make test-postgres
   ```

## Примечание по лицензиям

Для запуска `1cv8c` / `ragent` в контейнере нужна активная лицензия (например, бесплатная комьюнити-лицензия разработчика 1С). Каталог лицензий монтируется в Docker volume `1c-licenses` (`/var/1C/licenses`), чтобы активация сохранялась между перезапусками контейнеров.
