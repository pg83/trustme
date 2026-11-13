#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::Coroutine;

trait Finish {
    type Output;
    fn finish(self) -> Self::Output;
}

impl<A, G: Coroutine<()>> Finish for (Option<A>, G, G::Yield, G::Return) {
    type Output = G::Return;
    fn finish(self) -> Self::Output {
        self.3
    }
}

fn finish<T: Finish>(value: T) -> T::Output {
    value.finish()
}

fn main() {
    let marker = None;
    let value = Default::default();
    let coroutine = #[coroutine] move || {
        yield value;
        value
    };
    let result: u8 = finish((marker, coroutine, Default::default(), Default::default()));
    let _: Option<()> = marker;
    assert_eq!(result, 0);
}
