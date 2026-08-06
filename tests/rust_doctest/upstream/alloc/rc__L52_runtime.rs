// Extracted from library/alloc/src/rc.rs:52
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let rc = Rc::new(());
    // Method-call syntax
    let rc2 = rc.clone();
    // Fully qualified syntax
    let rc3 = Rc::clone(&rc);
}
