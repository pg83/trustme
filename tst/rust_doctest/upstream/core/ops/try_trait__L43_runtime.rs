// Extracted from library/core/src/ops/try_trait.rs:43
#![allow(unused)]
#![feature(try_trait_v2)]
fn main() {
    use std::ops::Try;
    fn simple_try_fold_1<A, T, R: Try<Output = A>>(
        iter: impl Iterator<Item = T>,
        mut accum: A,
        mut f: impl FnMut(A, T) -> R,
    ) -> R {
        todo!()
    }
}
