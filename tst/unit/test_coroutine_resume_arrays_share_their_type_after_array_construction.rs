#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

type Twin<T> = (T, T);

fn drive<T: Copy, const N: usize, G>(mut task: G, first: [T; N], second: [T; N]) -> (G::Yield, G::Return)
where
    G: Coroutine<Twin<[T; N]>> + Unpin,
{
    let yielded = match Pin::new(&mut task).resume((first, second)) {
        CoroutineState::Yielded(value) => value,
        CoroutineState::Complete(_) => panic!("completed before yielding"),
    };
    let returned = match Pin::new(&mut task).resume((second, first)) {
        CoroutineState::Complete(value) => value,
        CoroutineState::Yielded(_) => panic!("yielded instead of completing"),
    };
    (yielded, returned)
}

fn main() {
    let task = #[coroutine] |pair: Twin<[_; _]>| {
        let [first, _other] = [pair.0, pair.1];
        let next = yield first;
        next.0
    };
    assert_eq!(drive(task, [19u16, 23], [29u16, 31]), ([19, 23], [29, 31]));
}
