// Extracted from library/alloc/src/rc.rs:70
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let foo = Rc::new(vec![1.0, 2.0, 3.0]);
    // The two syntaxes below are equivalent.
    let a = foo.clone();
    let b = Rc::clone(&foo);
    // a and b both point to the same memory location as foo.
}
