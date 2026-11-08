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
        let next = yield pair.0;
        next.0
    };
    assert_eq!(drive(task, [7u16, 11], [13u16, 17]), ([7, 11], [13, 17]));
}
