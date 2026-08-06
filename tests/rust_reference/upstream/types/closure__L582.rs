// Extracted from src/types/closure.md:582
#![allow(unused)]
fn main() {
    // This is the same as the example above except the closure
    // moves the value instead of taking a reference to it.
    
    struct S(String);
    
    let b = Box::new(S(String::new()));
    let c_box = || {
        let x = (*b).0; // captures `b` with ByValue
    };
    c_box();
}
