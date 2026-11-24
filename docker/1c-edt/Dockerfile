# Образ 1C:EDT CLI для конвертации EDT-проектов (edt/) в XML Конфигуратора (build/xml/)
# Перед сборкой положите архив или .deb/.tar.gz дистрибутива 1C:EDT в docker/dist/edt/
# либо передайте BASE_IMAGE с установленной утилитой 1cedtcli / ring.
ARG BASE_IMAGE=ubuntu:22.04
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libglib2.0-0 \
    locales \
    openjdk-17-jre-headless \
    tar \
    unzip \
    && sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen \
    && locale-gen ru_RU.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

ENV LANG=ru_RU.UTF-8
ENV LC_ALL=ru_RU.UTF-8

COPY docker/dist/edt/ /tmp/edt-dist/
RUN if ls /tmp/edt-dist/*.deb >/dev/null 2>&1; then \
      dpkg -i /tmp/edt-dist/*.deb || (apt-get update && apt-get install -f -y); \
    elif ls /tmp/edt-dist/*.tar.gz >/dev/null 2>&1; then \
      mkdir -p /opt/1C/1CE/components && \
      tar -xzf /tmp/edt-dist/*.tar.gz -C /opt/1C/1CE/components; \
    fi && rm -rf /tmp/edt-dist /var/lib/apt/lists/*

WORKDIR /workspace
ENTRYPOINT ["/workspace/tools/scripts/export-edt-to-xml.sh"]
