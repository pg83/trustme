// Extracted from library/alloc/src/rc.rs:1614
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let five = Rc::new(5);
    let _also_five = Rc::clone(&five);
    
    assert_eq!(2, Rc::strong_count(&five));
}
