// Extracted from library/core/src/str/mod.rs:2766
#![allow(unused)]
fn main() {
    assert!("Ferris".eq_ignore_ascii_case("FERRIS"));
    assert!("Ferrös".eq_ignore_ascii_case("FERRöS"));
    assert!(!"Ferrös".eq_ignore_ascii_case("FERRÖS"));
}
