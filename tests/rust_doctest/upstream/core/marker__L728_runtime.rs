// Extracted from library/core/src/marker.rs:728
#![allow(unused)]
fn main() {
    use std::marker::PhantomData;
    
    #[allow(dead_code)]
    struct Slice<'a, T> {
        start: *const T,
        end: *const T,
        phantom: PhantomData<&'a T>,
    }
}
