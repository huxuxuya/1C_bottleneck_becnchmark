.PHONY: help download-yaxunit export-xml export-xml-docker build-edt build-ibcmd test-ibcmd test-local build-postgres up-postgres test-postgres down clean

COMPOSE := docker compose -f docker/docker-compose.yml

help:
	@echo "Доступные команды:"
	@echo "  make download-yaxunit   - Скачать YAxUnit.cfe в build/deps/"
	@echo "  make export-xml         - Экспортировать edt/ -> build/xml/ локальной 1cedtcli/ring"
	@echo "  make export-xml-docker  - Экспортировать edt/ -> build/xml/ в Docker-контейнере EDT"
	@echo "  make build-edt          - Собрать Docker-образ 1C:EDT CLI"
	@echo "  make build-ibcmd        - Собрать Docker-образ для профиля ibcmd"
	@echo "  make test-ibcmd         - Создать файловую ИБ через ibcmd и прогнать тесты YAxUnit"
	@echo "  make test-local         - Собрать отдельную локальную ИБ и прогнать тесты через установленную платформу 1С"
	@echo "  make build-postgres     - Собрать Docker-образы для профиля postgres"
	@echo "  make up-postgres        - Поднять PostgreSQL и Сервер 1С в фоне"
	@echo "  make test-postgres      - Создать ИБ в PostgreSQL и прогнать тесты YAxUnit"
	@echo "  make down               - Остановить контейнеры"
	@echo "  make clean              - Удалить артефакты сборки в build/"

download-yaxunit:
	@./tools/scripts/download-yaxunit.sh

export-xml:
	@./tools/scripts/export-edt-to-xml.sh

build-edt:
	$(COMPOSE) --profile edt-export build

export-xml-docker:
	$(COMPOSE) --profile edt-export run --rm edt-export

build-ibcmd:
	$(COMPOSE) --profile ibcmd build

test-ibcmd: download-yaxunit
	$(COMPOSE) --profile ibcmd run --rm runner-ibcmd

test-local: download-yaxunit
	WORKSPACE="$(CURDIR)" IB_DIR="$(CURDIR)/build/ib-local-test" ./tools/scripts/build-and-test.sh --mode ibcmd

build-postgres:
	$(COMPOSE) --profile postgres build

up-postgres:
	$(COMPOSE) --profile postgres up -d postgres 1c-server

test-postgres: download-yaxunit
	$(COMPOSE) --profile postgres run --rm runner-postgres

down:
	$(COMPOSE) --profile edt-export --profile ibcmd --profile postgres down

clean:
	rm -rf build/ib build/xml build/edt-workspace build/reports build/techlog
