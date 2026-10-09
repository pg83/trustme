//@ aux-build: reexported_core.rs
use reexported_core::concat_via_core;

fn main() {
    assert_eq!(concat_via_core!("hi"), "hi!");
    assert_eq!(reexported_core::core::stringify!(a + b), "a + b");
}
