//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::Coroutine;
use std::pin::Pin;

type Twice<T> = (T, T);

fn same<T>(_: &T, _: &T) {}

fn inspect<G, R>(_: &mut G)
where
    G: Coroutine<R, Yield = (), Return = ()>,
{}

pub fn exercise() {
    let mut coroutine = #[coroutine] |(a, b): Twice<_>| {
        same(&a, &b);
        yield ();
    };
    inspect(&mut coroutine);
    let _ = unsafe { Pin::new_unchecked(&mut coroutine) }.resume((1u8, 2u8));
}
