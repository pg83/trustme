// Extracted from src/attributes/diagnostics.md:415
#![allow(unused)]
#![deny(unused_must_use)]
fn main() {
    #[must_use]
    fn f() {}
    f(); // ERROR: Unused return value that must be used.
}
