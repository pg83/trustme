//@ proc-macro-aux-build: cov99_w431_truncated_bound.rs
//@ compile-fail: Unexpected token
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-pm

use cov99_w431_truncated_bound::truncated_bound;

truncated_bound!();

fn main() {}
