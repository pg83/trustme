// Extracted from library/core/src/ops/try_trait.rs:24
#![allow(unused)]
fn main() {
    fn simple_fold<A, T>(
        iter: impl Iterator<Item = T>,
        mut accum: A,
        mut f: impl FnMut(A, T) -> A,
    ) -> A {
        for x in iter {
            accum = f(accum, x);
        }
        accum
    }
}
