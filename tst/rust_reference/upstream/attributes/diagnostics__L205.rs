// Extracted from src/attributes/diagnostics.md:205
#![allow(unused)]
fn main() {
    // This expectation will be fulfilled by the unused value inside the function
    // since the emitted `unused_variables` lint is inside the `unused` lint group.
    #[expect(unused)]
    pub fn thoughts() {
        let unused = "I'm running out of examples";
    }
    
    pub fn another_example() {
        // This attribute creates two lint expectations. The `unused_mut` lint will be
        // suppressed and with that fulfill the first expectation. The `unused_variables`
        // wouldn't be emitted, since the variable is used. That expectation will therefore
        // be unsatisfied, and a warning will be emitted.
        #[expect(unused_mut, unused_variables)]
        let mut link = "https://www.rust-lang.org/";
    
        println!("Welcome to our community: {link}");
    }
}
