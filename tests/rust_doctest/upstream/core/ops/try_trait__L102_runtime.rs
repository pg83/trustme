// Extracted from library/core/src/ops/try_trait.rs:102
#![allow(unused)]
#![feature(try_trait_v2)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::ops::Try;
        fn simple_try_fold<A, T, R: Try<Output = A>>(
            iter: impl Iterator<Item = T>,
            mut accum: A,
            mut f: impl FnMut(A, T) -> R,
        ) -> R {
            for x in iter {
                accum = f(accum, x)?;
            }
            R::from_output(accum)
        }
        Ok(())
    }
    doctest().unwrap();
}
