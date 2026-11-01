//@ aux-build: reexported_payload_use_targets.rs
#![feature(decl_macro)]

mod facade { pub use reexported_payload_use_targets::Payload; }

macro read_variants() {{
    use crate::facade::Payload::{Plain, Pair, Named};
    let first = match Pair(6, 7) {
        Pair(a, b) => a as u16 * b,
        _ => 0,
    };
    let second = match (Named { value: 42 }) {
        Named { value } => value,
        _ => 0,
    };
    assert!(matches!(Plain, Plain));
    (first, second)
}}

fn main() { assert_eq!(read_variants!(), (42, 42)); }
