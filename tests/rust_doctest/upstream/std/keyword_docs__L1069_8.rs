// Extracted from library/std/src/keyword_docs.rs:1069
#![allow(unused)]
fn main() {
    // A mutable variable in the parameter list of a function.
    fn foo(mut x: u8, y: u8) -> u8 {
        x += y;
        x
    }
    
    // Modifying a mutable variable.
    #[allow(unused_assignments)]
    let mut a = 5;
    a = 6;
    
    assert_eq!(foo(3, 4), 7);
    assert_eq!(a, 6);
}
