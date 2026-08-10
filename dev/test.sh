#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

exec env -u CC -u CXX "$root/../ix/ix" run set/pg/libs -- \
    env SSL_CERT_FILE=/etc/ssl/cert.pem "$root/build" test
