// Extracted from library/core/src/ops/try_trait.rs:57
#![allow(unused)]
#![feature(try_trait_v2)]
fn main() {
    use std::ops::{ControlFlow, Try};
    fn simple_try_fold_2<A, T, R: Try<Output = A>>(
        iter: impl Iterator<Item = T>,
        mut accum: A,
        mut f: impl FnMut(A, T) -> R,
    ) -> R {
        for x in iter {
            let cf = f(accum, x).branch();
            match cf {
                ControlFlow::Continue(a) => accum = a,
                ControlFlow::Break(_) => todo!(),
            }
        }
        R::from_output(accum)
    }
}
