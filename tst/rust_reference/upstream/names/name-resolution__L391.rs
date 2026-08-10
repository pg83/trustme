// Extracted from src/names/name-resolution.md:391
#![allow(unused)]
fn main() {
    macro_rules! define_ambig {
        () => {
            macro_rules! ambig {
                () => {}
            }
        }
    }
    
    // Introduce outer candidate definition for `ambig` macro invocation.
    macro_rules! ambig {
        () => {}
    }
    
    // Introduce a second candidate definition for `ambig` inside of a
    // macro expansion.
    define_ambig!();
    
    // The definition of `ambig` from the second invocation
    // of `define_ambig` is the innermost canadidate.
    //
    // The definition of `ambig` from the first invocation of
    // `define_ambig` is the second candidate.
    //
    // The compiler checks that the first candidate is inside of a macro
    // expansion, that the second candidate is not from within the same
    // macro expansion, and that the name being resolved is not from
    // within the same macro expansion.
    ambig!(); // ERROR: `ambig` is ambiguous.
}
