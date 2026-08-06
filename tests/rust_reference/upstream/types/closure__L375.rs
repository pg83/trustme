// Extracted from src/types/closure.md:375
#![allow(unused)]
fn main() {
    let x: &mut [u8] = &mut [];
    let c = || match x { // Captures `*x` by `ImmBorrow`.
        &mut [] => (),
    //       ^^
    // This matches a slice of exactly zero elements. To know whether the
    // scrutinee matches, the length must be read, causing the slice to
    // be captured.
        _ => (),
    };
    let _ = &mut *x; // ERROR: Cannot borrow `*x` as mutable.
    c();
}
