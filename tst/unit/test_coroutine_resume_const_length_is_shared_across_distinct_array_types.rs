#![feature(coroutines, coroutine_trait, generic_arg_infer, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

type Packet<const N: usize> = ([u8; N], [u16; N]);

fn drive<const N: usize, G>(mut task: G, first: Packet<N>, second: Packet<N>) -> (u16, u16)
where
    G: Coroutine<Packet<N>, Yield = u16, Return = u16> + Unpin,
{
    let yielded = match Pin::new(&mut task).resume(first) {
        CoroutineState::Yielded(value) => value,
        CoroutineState::Complete(_) => panic!("completed before yielding"),
    };
    let returned = match Pin::new(&mut task).resume(second) {
        CoroutineState::Complete(value) => value,
        CoroutineState::Yielded(_) => panic!("yielded instead of completing"),
    };
    (yielded, returned)
}

fn main() {
    let task = #[coroutine] |pair: Packet<_>| {
        let next = yield u16::from(pair.0[0]) + pair.1[1];
        u16::from(next.0[1]) + next.1[0]
    };
    assert_eq!(drive(task, ([5, 7], [11, 13]), ([17, 19], [23, 29])), (18, 42));
}
