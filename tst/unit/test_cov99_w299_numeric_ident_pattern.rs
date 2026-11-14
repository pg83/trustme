//@ crate-type: lib
//@ proc-macro-aux-build: cov99_w299_numeric_ident_pattern.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-pm

pub struct Name(pub u8);

cov99_w299_numeric_ident_pattern::emit!();
