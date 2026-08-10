// Extracted from src/const_eval.md:107
#![allow(unused)]
#![allow(static_mut_refs)]
fn main() {
      // Even though this borrow is mutable, it's not of a temporary, so
      // this is allowed.
      const C: &u8 = unsafe { static mut S: u8 = 0; &mut S }; // OK
}
