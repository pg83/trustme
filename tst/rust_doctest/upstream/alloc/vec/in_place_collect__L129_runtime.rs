// Extracted from library/alloc/src/vec/in_place_collect.rs:129
#![allow(unused)]
extern crate alloc;
fn main() {
    #[allow(dead_code)]
    /// Drops remaining items in `src` and if the layouts of `T` and `U` match it
    /// returns an empty Vec backed by the original allocation. Otherwise it returns a new
    /// empty vec.
    pub fn recycle_allocation<T, U>(src: Vec<T>) -> Vec<U> {
      src.into_iter().filter_map(|_| None).collect()
    }
}
