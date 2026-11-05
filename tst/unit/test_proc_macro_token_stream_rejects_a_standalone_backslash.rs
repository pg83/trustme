//@ proc-macro-aux-build: parsed_symbol_stringification.rs

fn main() {
    assert_eq!(parsed_symbol_stringification::backslash!(), "rejected");
}
