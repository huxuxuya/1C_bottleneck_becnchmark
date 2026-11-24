# Вариант 1: Образ для запуска ibcmd + headless клиента 1С (YAxUnit) без полноценного кластера.
# Положите .deb-пакеты платформы 1С (server, common, client/thin-client) в docker/dist/
# либо передайте BASE_IMAGE с уже установленной платформой 1С.
ARG BASE_IMAGE=ubuntu:22.04
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    fontconfig \
    libfreetype6 \
    libglib2.0-0 \
    libgsf-1-114 \
    libodbc1 \
    libx11-6 \
    libxcb1 \
    libxrender1 \
    locales \
    xauth \
    xvfb \
    && sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen \
    && locale-gen ru_RU.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

ENV LANG=ru_RU.UTF-8
ENV LC_ALL=ru_RU.UTF-8

# Установка .deb-пакетов 1С, если они скопированы в docker/dist/
COPY docker/dist/ /tmp/1c-dist/
RUN if ls /tmp/1c-dist/*.deb >/dev/null 2>&1; then \
      dpkg -i /tmp/1c-dist/*.deb || apt-get update && apt-get install -f -y; \
    fi && rm -rf /tmp/1c-dist /var/lib/apt/lists/*

WORKDIR /workspace
ENTRYPOINT ["/bin/bash"]
