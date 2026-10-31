use std::mem::{align_of, align_of_val, needs_drop, size_of, size_of_val};
struct Dropped(u8);
impl Drop for Dropped { fn drop(&mut self) {} }
fn layout<T>(value: &T) -> (usize, usize, usize, usize, bool) {
    (size_of::<T>(), size_of_val(value), align_of::<T>(), align_of_val(value), needs_drop::<T>())
}
fn bytes(value: u8) -> (u8, u8) { (value.swap_bytes(), 91u8.swap_bytes()) }
fn main() {
    assert_eq!(layout(&[2u8, 3, 5]), (3, 3, 1, 1, false));
    let owned = Dropped(7);
    assert_eq!(layout(&owned), (1, 1, 1, 1, true));
    assert_eq!(bytes(owned.0), (7, 91));
}
