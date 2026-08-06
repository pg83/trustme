// Extracted from src/names/name-resolution.md:332
#![allow(unused)]
fn main() {
    // Textual macro candidate.
    macro_rules! ambig {
        () => {}
    }
    
    // Path-based macro candidate.
    macro_rules! path_based {
        () => {}
    }
    
    pub fn f() {
        // This reexport of the `path_based` macro definition
        // as `ambig` may not shadow the `ambig` macro definition
        // which is resolved via textual macro scope.
        use path_based as ambig;
        ambig!(); // ERROR: `ambig` is ambiguous.
    }
}
