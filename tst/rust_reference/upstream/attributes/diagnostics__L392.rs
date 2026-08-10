// Extracted from src/attributes/diagnostics.md:392
#![allow(unused)]
#![deny(unused_must_use)]
fn main() {
    #[must_use]
    struct MustUse();
    MustUse(); // ERROR: Unused value that must be used.
}
