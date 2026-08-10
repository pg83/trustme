// Extracted from src/const_eval.md:82
#![allow(unused)]
fn main() {
    // Due to being in tail position, this borrow extends the scope of the
      // temporary to the end of the program. Since the borrow is mutable,
      // this is not allowed in a const expression.
      const C: &u8 = &mut 0; // ERROR not allowed
}
