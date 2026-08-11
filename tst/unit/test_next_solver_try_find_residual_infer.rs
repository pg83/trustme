//@ check-pass
//@ compile-flags: -Znext-solver

#![feature(try_trait_v2)]

use std::ops::{ControlFlow, FromResidual, Residual, Try};

type ChangeOutputType<T, V> = <<T as Try>::Residual as Residual<V>>::TryType;

fn try_find<I, R>(
    iter: &mut I,
    f: impl FnMut(&I::Item) -> R,
) -> ChangeOutputType<R, Option<I::Item>>
where
    I: Iterator,
    R: Try<Output = bool>,
    R::Residual: Residual<Option<I::Item>>,
{
    fn check<I, V, R>(
        mut f: impl FnMut(&I) -> V,
    ) -> impl FnMut((), I) -> ControlFlow<R::TryType>
    where
        V: Try<Output = bool, Residual = R>,
        R: Residual<Option<I>>,
    {
        move |(), x| match f(&x).branch() {
            ControlFlow::Continue(false) => ControlFlow::Continue(()),
            ControlFlow::Continue(true) => ControlFlow::Break(Try::from_output(Some(x))),
            ControlFlow::Break(r) => ControlFlow::Break(FromResidual::from_residual(r)),
        }
    }

    match iter.try_fold((), check(f)) {
        ControlFlow::Break(x) => x,
        ControlFlow::Continue(()) => Try::from_output(None),
    }
}

fn main() {
    let mut values = [1, 2, 3].into_iter();
    let found: Option<Option<i32>> = try_find(&mut values, |value| Some(*value == 2));
    assert_eq!(found, Some(Some(2)));
}
