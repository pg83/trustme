// Extracted from src/names/name-resolution.md:444
#![allow(unused)]
fn main() {
    macro_rules! ambig {
        () => {}
    }
    
    macro_rules! define_and_invoke_ambig {
        () => {
            // Define innermost candidate.
            macro_rules! ambig {
                () => {}
            }
    
            // Invocation of `ambig` is in the same expansion as the
            // innermost candidate.
            ambig!(); // OK
        }
    }
    
    define_and_invoke_ambig!();
}
