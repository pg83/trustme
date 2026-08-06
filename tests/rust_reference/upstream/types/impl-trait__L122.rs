// Extracted from src/types/impl-trait.md:122
#![allow(unused)]
fn main() {
    fn capture<'a, 'b, T>(x: &'a (), y: T) -> impl Sized + use<'a, T> {
      //                                      ~~~~~~~~~~~~~~~~~~~~~~~
      //                                     Captures `'a` and `T` only.
      (x, y)
    }
}
