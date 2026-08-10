// Extracted from library/core/src/marker.rs:745
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    use std::marker::PhantomData;
    struct Slice<'a, T> {
        start: *const T,
        end: *const T,
        phantom: PhantomData<&'a T>,
    }
    fn borrow_vec<T>(vec: &Vec<T>) -> Slice<'_, T> {
        let ptr = vec.as_ptr();
        Slice {
            start: ptr,
            end: unsafe { ptr.add(vec.len()) },
            phantom: PhantomData,
        }
    }
}
