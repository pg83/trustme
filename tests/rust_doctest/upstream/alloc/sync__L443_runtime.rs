// Extracted from library/alloc/src/sync.rs:443
#![allow(unused)]
#![allow(dead_code)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak};
    
    struct Gadget {
        me: Weak<Gadget>,
    }
    
    impl Gadget {
        /// Constructs a reference counted Gadget.
        fn new() -> Arc<Self> {
            // `me` is a `Weak<Gadget>` pointing at the new allocation of the
            // `Arc` we're constructing.
            Arc::new_cyclic(|me| {
                // Create the actual struct here.
                Gadget { me: me.clone() }
            })
        }
    
        /// Returns a reference counted pointer to Self.
        fn me(&self) -> Arc<Self> {
            self.me.upgrade().unwrap()
        }
    }
}
