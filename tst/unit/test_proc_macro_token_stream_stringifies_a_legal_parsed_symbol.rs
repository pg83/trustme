//@ proc-macro-aux-build: parsed_symbol_stringification.rs
//@ compile-variant: -O

fn main() {
    assert_eq!(parsed_symbol_stringification::legal!(), "+");
}
