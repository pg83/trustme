// Extracted from src/macros-by-example.md:251
#![allow(unused)]
fn main() {
    fn foo() {
        // m!(); // Error: m is not in scope.
        macro_rules! m {
            () => {};
        }
        m!();
    }
    
    // m!(); // Error: m is not in scope.
}
