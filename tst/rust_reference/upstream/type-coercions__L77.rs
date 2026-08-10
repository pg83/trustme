// Extracted from src/type-coercions.md:77
#![allow(unused)]
fn main() {
    let mut x = &0i8;
      let y = &mut 42i8;
      x = y;
}
