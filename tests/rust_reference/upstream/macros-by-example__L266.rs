// Extracted from src/macros-by-example.md:266
#![allow(unused)]
fn main() {
    macro_rules! m2 {
        () => {
            println!("m2");
        };
    }
    
    // Resolves to path-based candidate from use declaration below.
    m!(); // prints "m2\n"
    
    // Introduce second candidate for `m` with textual scope.
    //
    // This shadows path-based candidate from below for the rest of this
    // example.
    macro_rules! m {
        () => {
            println!("m");
        };
    }
    
    // Introduce `m2` macro as path-based candidate.
    //
    // This item is in scope for this entire example, not just below the
    // use declaration.
    use m2 as m;
    
    // Resolves to the textual macro candidate from above the use
    // declaration.
    m!(); // prints "m\n"
}
