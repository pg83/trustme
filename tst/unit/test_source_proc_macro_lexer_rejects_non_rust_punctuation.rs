//@ aux-build: ../../../lib/proc_macro/src/lib.rs
//@ compile-variant: -O
//@ run-fail: procedural macro API is used outside of a procedural macro

extern crate lib as source_proc_macro;

fn main() {
    for invalid in ["\\", "`", "left \\ right", "left`right"] {
        assert!(invalid.parse::<source_proc_macro::TokenStream>().is_err());
    }
    for valid in ["+", "~@", "left::right", "(left, right)"] {
        let stream = valid.parse::<source_proc_macro::TokenStream>().unwrap();
        assert!(!stream.is_empty());
        assert!(!stream.to_string().is_empty());
    }
}
