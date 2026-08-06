// Extracted from library/core/src/marker.rs:346
#![allow(unused)]
fn main() {
    #[derive(Clone)]
    struct MyStruct<T>(T);
    
    impl<T: Copy> Copy for MyStruct<T> { }
}
