// Extracted from library/alloc/src/vec/in_place_collect.rs:139
#![allow(unused)]
extern crate alloc;
fn main() {
    let vec = vec![13usize; 1024];
    let _ = vec.into_iter()
      .enumerate()
      .filter_map(|(idx, val)| if idx % 2 == 0 { Some(val+idx) } else {None})
      .collect::<Vec<_>>();
    
    // is equivalent to the following, but doesn't require bounds checks
    
    let mut vec = vec![13usize; 1024];
    let mut write_idx = 0;
    for idx in 0..vec.len() {
       if idx % 2 == 0 {
          vec[write_idx] = vec[idx] + idx;
          write_idx += 1;
       }
    }
    vec.truncate(write_idx);
}
