// The HIR is printed once names are resolved; only the `typed` variant needs
// the types as well. A crate that only makes sense before type checking is
// still a valid input to `-Zunpretty=hir`.
//@ crate-type: lib
//@ compile-flags: -Zunpretty=hir

#[deprecated(since = "1.0.0", note = "gone")]
pub struct Gone;

pub fn mismatched() -> u32 {
    let x: u8 = 1;
    x
}
