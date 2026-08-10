// Extracted from library/core/src/ptr/mod.rs:2484
#![allow(unused)]
fn main() {
    let f: fn(i32) -> i32 = |x| x;
      let g: fn(i32) -> i32 = |x| x + 0;  // different closure, different body
      let h: fn(u32) -> u32 = |x| x + 0;  // different signature too
      dbg!(std::ptr::fn_addr_eq(f, g), std::ptr::fn_addr_eq(f, h)); // not guaranteed to be equal
}
