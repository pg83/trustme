// Extracted from library/core/src/cell.rs:2071
#![allow(unused)]
#![forbid(unsafe_code)] // with exclusive accesses,
fn main() {
                            // `UnsafeCell` is a transparent no-op wrapper,
                            // so no need for `unsafe` here.
    use std::cell::UnsafeCell;

    let mut x: UnsafeCell<i32> = 42.into();

    // Get a compile-time-checked unique reference to `x`.
    let p_unique: &mut UnsafeCell<i32> = &mut x;
    // With an exclusive reference, we can mutate the contents for free.
    *p_unique.get_mut() = 0;
    // Or, equivalently:
    x = UnsafeCell::new(0);

    // When we own the value, we can extract the contents for free.
    let contents: i32 = x.into_inner();
    assert_eq!(contents, 0);
}
