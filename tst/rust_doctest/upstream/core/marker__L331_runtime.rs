// Extracted from library/core/src/marker.rs:331
#![allow(unused)]
fn main() {
    struct MyStruct;

    impl Copy for MyStruct { }

    impl Clone for MyStruct {
        fn clone(&self) -> MyStruct {
            *self
        }
    }
}
