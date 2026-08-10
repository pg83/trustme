// Extracted from src/macros-by-example.md:57
#![allow(unused)]
fn main() {
    macro_rules! ambiguity {
        ($($i:ident)* $j:ident) => { };
    }
    
    ambiguity!(error); // Error: local ambiguity
}
