// Extracted from library/alloc/src/sync.rs:1454
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let x: Arc<[u32]> = Arc::new([1, 2, 3]);
    let x_ptr: *const [u32] = Arc::into_raw(x);

    unsafe {
        let x: Arc<[u32; 3]> = Arc::from_raw(x_ptr.cast::<[u32; 3]>());
        assert_eq!(&*x, &[1, 2, 3]);
    }
}
