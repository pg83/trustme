//@ compile-flags: -Zdump-hir -Zdump-mir

use core::ops::{Index, IndexMut};

struct Pair([i32; 2]);

impl Index<usize> for Pair {
    type Output = i32;

    fn index(&self, index: usize) -> &i32 {
        &self.0[index]
    }
}

impl IndexMut<usize> for Pair {
    fn index_mut(&mut self, index: usize) -> &mut i32 {
        &mut self.0[index]
    }
}

fn main() {
    let mut pair = Pair([3, 5]);
    *<Pair as IndexMut<usize>>::index_mut(&mut pair, 0) = 17;
    pair[1] += 6;
    assert_eq!(pair[0], 17);
    assert_eq!(pair[1], 11);
}
