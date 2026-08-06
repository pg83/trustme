#!/bin/sh

set -eu

exec env -u CC -u CXX ../ix/ix run set/pg/libs -- \
    env SSL_CERT_FILE=/etc/ssl/cert.pem ./build test
