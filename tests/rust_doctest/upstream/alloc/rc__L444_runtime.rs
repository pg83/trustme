// Extracted from library/alloc/src/rc.rs:444
#![allow(unused)]
#![allow(dead_code)]
extern crate alloc;
fn main() {
    use std::rc::{Rc, Weak};
    
    struct Gadget {
        me: Weak<Gadget>,
    }
    
    impl Gadget {
        /// Constructs a reference counted Gadget.
        fn new() -> Rc<Self> {
            // `me` is a `Weak<Gadget>` pointing at the new allocation of the
            // `Rc` we're constructing.
            Rc::new_cyclic(|me| {
                // Create the actual struct here.
                Gadget { me: me.clone() }
            })
        }
    
        /// Returns a reference counted pointer to Self.
        fn me(&self) -> Rc<Self> {
            self.me.upgrade().unwrap()
        }
    }
}
