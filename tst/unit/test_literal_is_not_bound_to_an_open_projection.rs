/* rayon's `producer_split_at` tests: `check(&a, || a)` with `fn check<I, F>(expected: &[I::Item],
   f: F) where I: IntoParIter, F: FnMut() -> I` - the closure's return is what decides `I`, and
   the array `a` of literals is coerced to `&[I::Item]` before `I` is known, and the literal
   element must not become that open projection - it would tie `I` to itself. */
use std::fmt::Debug;

trait IntoParIter {
    type Item;
    fn items(self) -> Vec<Self::Item>;
}

impl<T: Copy, const N: usize> IntoParIter for [T; N] {
    type Item = T;
    fn items(self) -> Vec<T> {
        self.to_vec()
    }
}

fn check<I, F>(expected: &[I::Item], mut f: F)
where
    I: IntoParIter,
    I::Item: PartialEq + Debug,
    F: FnMut() -> I,
{
    assert_eq!(f().items(), expected);
}

fn main() {
    let a = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
    check(&a, || a);
}
