//@ aux-build: cov99_w1212_associated_const_metadata.rs
//@ compile-fail: Drop
//@ compile-flags: -Cpanic=abort

use cov99_w1212_associated_const_metadata::{View, Width, Word};

struct Owner<T: View>(T);

impl<T: View<Out = Width<N>>, const N: usize> Drop for Owner<T> {
    fn drop(&mut self) {
        std::hint::black_box(N);
    }
}

fn main() {
    let _owner = Owner(Word::<3>);
}
