// Extracted from library/alloc/src/rc.rs:3695
#![allow(unused)]
#![feature(unique_rc_arc)]
extern crate alloc;
fn main() {
    use std::rc::{Rc, Weak, UniqueRc};
    
    struct Gadget {
        #[allow(dead_code)]
        me: Weak<Gadget>,
    }
    
    fn create_gadget() -> Option<Rc<Gadget>> {
        let mut rc = UniqueRc::new(Gadget {
            me: Weak::new(),
        });
        rc.me = UniqueRc::downgrade(&rc);
        Some(UniqueRc::into_rc(rc))
    }
    
    create_gadget().unwrap();
}
