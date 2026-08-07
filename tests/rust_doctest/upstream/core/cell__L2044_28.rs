// Extracted from library/core/src/cell.rs:2044
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;

    let x: UnsafeCell<i32> = 42.into();
    // Get multiple / concurrent / shared references to the same `x`.
    let (p1, p2): (&UnsafeCell<i32>, &UnsafeCell<i32>) = (&x, &x);

    unsafe {
        // SAFETY: within this scope there are no other references to `x`'s contents,
        // so ours is effectively unique.
        let p1_exclusive: &mut i32 = &mut *p1.get(); // -- borrow --+
        *p1_exclusive += 27; //                                     |
    } // <---------- cannot go beyond this point -------------------+

    unsafe {
        // SAFETY: within this scope nobody expects to have exclusive access to `x`'s contents,
        // so we can have multiple shared accesses concurrently.
        let p2_shared: &i32 = &*p2.get();
        assert_eq!(*p2_shared, 42 + 27);
        let p1_shared: &i32 = &*p1.get();
        assert_eq!(*p1_shared, *p2_shared);
    }
}
