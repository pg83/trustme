// Extracted from src/names/name-resolution.md:424
#![allow(unused)]
fn main() {
    macro_rules! define_ambig {
        () => {
            macro_rules! ambig {
                () => {}
            }
        }
    }
    // Swap order of definitions.
    define_ambig!();
    macro_rules! ambig {
        () => {}
    }
    // The innermost candidate is now less expanded so it may shadow more
    // the macro expanded definition above it.
    ambig!();
}
