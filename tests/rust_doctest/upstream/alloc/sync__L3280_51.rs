// Extracted from library/alloc/src/sync.rs:3280
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak};

    struct Foo;

    impl Drop for Foo {
        fn drop(&mut self) {
            println!("dropped!");
        }
    }

    let foo = Arc::new(Foo);
    let weak_foo = Arc::downgrade(&foo);
    let other_weak_foo = Weak::clone(&weak_foo);

    drop(weak_foo);   // Doesn't print anything
    drop(foo);        // Prints "dropped!"

    assert!(other_weak_foo.upgrade().is_none());
}
