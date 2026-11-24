# Вариант 2: Образ сервера 1С (ragent + rmngr + rphost + ibcmd) для работы в связке с PostgreSQL.
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
    locales \
    && sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen \
    && locale-gen ru_RU.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

ENV LANG=ru_RU.UTF-8
ENV LC_ALL=ru_RU.UTF-8

COPY docker/dist/ /tmp/1c-dist/
RUN if ls /tmp/1c-dist/*.deb >/dev/null 2>&1; then \
      dpkg -i /tmp/1c-dist/*.deb || apt-get update && apt-get install -f -y; \
    fi && rm -rf /tmp/1c-dist /var/lib/apt/lists/*

COPY docker/techlog/logcfg.xml /opt/1cv8/conf/logcfg.xml

EXPOSE 1540 1541 1545 1560-1591

CMD ["/bin/bash", "-c", "RAGENT=$(find /opt/1cv8 -type f -name ragent | head -n 1); RAS=$(find /opt/1cv8 -type f -name ras | head -n 1); \"$RAS\" cluster --daemon localhost:1540; exec \"$RAGENT\" -d /var/1C/srvinfo"]
