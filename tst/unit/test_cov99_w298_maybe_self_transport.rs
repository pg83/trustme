//@ proc-macro-aux-build: cov99_w298_maybe_self.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-macro

cov99_w298_maybe_self::seed!();

pub const MARKER: u32 = 298;
