// Extracted from src/const_eval.md:101
#![allow(unused)]
fn main() {
    use core::sync::atomic::AtomicU8;
      // As above.
      let _: &_ = const { &AtomicU8::new(0) }; // ERROR not allowed
}
