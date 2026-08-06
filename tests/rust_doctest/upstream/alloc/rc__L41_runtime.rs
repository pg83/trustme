// Extracted from library/alloc/src/rc.rs:41
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let my_rc = Rc::new(());
    let my_weak = Rc::downgrade(&my_rc);
}
