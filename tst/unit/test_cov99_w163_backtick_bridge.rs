//@ proc-macro-aux-build: cov99_w163_backtick_bridge.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-procmacro

use cov99_w163_backtick_bridge::{consume, seed};

seed!();

fn main() {}
