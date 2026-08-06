// Extracted from src/const_eval.md:94
#![allow(unused)]
fn main() {
    use core::sync::atomic::AtomicU8;
      // This is not allowed as 1) the temporary scope is extended to the
      // end of the program and 2) the temporary has interior mutability.
      const C: &AtomicU8 = &AtomicU8::new(0); // ERROR not allowed
}
