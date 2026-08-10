// Extracted from library/alloc/src/rc.rs:2291
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    struct Foo;

    impl Drop for Foo {
        fn drop(&mut self) {
            println!("dropped!");
        }
    }

    let foo  = Rc::new(Foo);
    let foo2 = Rc::clone(&foo);

    drop(foo);    // Doesn't print anything
    drop(foo2);   // Prints "dropped!"
}
