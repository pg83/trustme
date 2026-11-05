//@ proc-macro-aux-build: strict_parsed_symbol_rejection.rs
//@ compile-fail: error:

fn main() {
    let _ = strict_parsed_symbol_rejection::backslash!();
}
