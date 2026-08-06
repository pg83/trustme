// Extracted from src/const_eval.md:114
#![allow(unused)]
fn main() {
    use core::sync::atomic::AtomicU8;
      // Even though this borrow is of a value with interior mutability,
      // it's not of a temporary, so this is allowed.
      const C: &AtomicU8 = {
          static S: AtomicU8 = AtomicU8::new(0); &S // OK
      };
}
