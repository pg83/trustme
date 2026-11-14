//@ proc-macro-aux-build: cov99_w125_symbol_underscore.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-w125

extern crate cov99_w125_symbol_underscore;

fn main() {
    match 7 {
        cov99_w125_symbol_underscore::wildcard!() => {}
    }
}
