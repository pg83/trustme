// Extracted from library/alloc/src/rc.rs:3453
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::{Rc, Weak};

    struct Foo;

    impl Drop for Foo {
        fn drop(&mut self) {
            println!("dropped!");
        }
    }

    let foo = Rc::new(Foo);
    let weak_foo = Rc::downgrade(&foo);
    let other_weak_foo = Weak::clone(&weak_foo);

    drop(weak_foo);   // Doesn't print anything
    drop(foo);        // Prints "dropped!"

    assert!(other_weak_foo.upgrade().is_none());
}
