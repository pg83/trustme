// Extracted from library/alloc/src/vec/in_place_collect.rs:118
#![allow(unused)]
extern crate alloc;
fn main() {
    #[allow(dead_code)]
    /// Converts a usize vec into an isize one.
    pub fn cast(vec: Vec<usize>) -> Vec<isize> {
      // Does not allocate, free or panic. On optlevel>=2 it does not loop.
      // Of course this particular case could and should be written with `into_raw_parts` and
      // `from_raw_parts` instead.
      vec.into_iter().map(|u| u as isize).collect()
    }
}
