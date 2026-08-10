// Extracted from library/alloc/src/sync.rs:2621
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    struct Foo;

    impl Drop for Foo {
        fn drop(&mut self) {
            println!("dropped!");
        }
    }

    let foo  = Arc::new(Foo);
    let foo2 = Arc::clone(&foo);

    drop(foo);    // Doesn't print anything
    drop(foo2);   // Prints "dropped!"
}
