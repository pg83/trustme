//@ aux-build: builtin_macro_bridge.rs
//@ compile-variant: -O

#[macro_use]
extern crate builtin_macro_bridge;

fn main() {
    assert_eq!(join_text!("oak", ":", 17), "oak:17");
    assert_eq!(spell_tokens!(seed + 3), "seed + 3");
    assert!(configuration!(all()));
    assert!(!configuration!(any()));
}
