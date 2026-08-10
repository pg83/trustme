// Extracted from src/names/name-resolution.md:467
#![allow(unused)]
fn main() {
    macro_rules! define_ambig {
        () => {
            macro_rules! ambig {
                () => {}
            }
        }
    }
    define_ambig!();
    define_ambig!();
    ambig!(); // ERROR: `ambig` is ambiguous.
}
