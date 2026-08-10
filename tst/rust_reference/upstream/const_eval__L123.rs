// Extracted from src/const_eval.md:123
#![allow(unused)]
fn main() {
    use core::sync::atomic::AtomicU8;
      // This shared borrow of an interior mutable temporary is allowed
      // because its scope is not extended.
      const C: () = { _ = &AtomicU8::new(0); }; // OK
}
