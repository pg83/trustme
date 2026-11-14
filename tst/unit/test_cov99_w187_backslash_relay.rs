//@ proc-macro-aux-build: cov99_w187_backslash_relay.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-procmacro

use cov99_w187_backslash_relay::relay;

relay!();

fn main() {}
