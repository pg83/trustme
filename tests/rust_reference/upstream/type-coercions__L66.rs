// Extracted from src/type-coercions.md:66
#![allow(unused)]
fn main() {
    use std::fmt::Display;
      fn foo(x: &u32) -> &dyn Display {
          x
      }
}
