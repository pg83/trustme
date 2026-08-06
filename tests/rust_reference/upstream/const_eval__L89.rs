// Extracted from src/const_eval.md:89
#![allow(unused)]
fn main() {
    // Const blocks are similar to initializers of `const` items.
      let _: &u8 = const { &mut 0 }; // ERROR not allowed
}
