//@ compile-variant: -O
#![feature(unsafe_binders)]
#![allow(incomplete_features, unused_unsafe)]
fn main() {
    let values = [17u32, 43, 73];
    let slice: &[u32] = &values;
    let callback: fn(u32) -> u32 = |n| n + 7;
    assert_eq!(callback(slice[1]), 50);
    let mut value = 83u32;
    let unique = &mut value;
    let shared: &u32 = unique;
    assert_eq!(*shared, 83);
    unsafe {
        let bound: unsafe<'a> &'a u32 = std::unsafe_binder::wrap_binder!(&values[2]);
        assert_eq!(*std::unsafe_binder::unwrap_binder!(bound), 73);
    }
}
