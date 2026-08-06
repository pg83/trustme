// Extracted from library/alloc/src/sync.rs:4135
#![allow(unused)]
#![feature(unique_rc_arc)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak, UniqueArc};
    
    struct Gadget {
        me: Weak<Gadget>,
    }
    
    fn create_gadget() -> Option<Arc<Gadget>> {
        let mut rc = UniqueArc::new(Gadget {
            me: Weak::new(),
        });
        rc.me = UniqueArc::downgrade(&rc);
        Some(UniqueArc::into_arc(rc))
    }
    
    create_gadget().unwrap();
}
