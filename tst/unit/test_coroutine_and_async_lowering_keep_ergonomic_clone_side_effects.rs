#![feature(coroutines, coroutine_trait, stmt_expr_attributes, use_cloned)]

use std::cell::Cell;
use std::clone::UseCloned;
use std::future::Future;
use std::ops::{Coroutine, CoroutineState};
use std::rc::Rc;
use std::task::{Context, Poll, Waker};

struct Token {
    clones: Rc<Cell<u16>>,
    value: u16,
}

impl Clone for Token {
    fn clone(&self) -> Self {
        self.clones.set(self.clones.get() + 1);
        Self { clones: self.clones.clone(), value: self.value }
    }
}

impl UseCloned for Token {}

impl Token {
    fn read(&self) -> u16 { self.value }
}

fn main() {
    let clones = Rc::new(Cell::new(0));
    let token = Token { clones: clones.clone(), value: 43 };
    let mut coroutine = std::pin::pin!(#[coroutine] move |bias: u16| {
        let selected = token.use;
        let tail = yield selected.read() + bias;
        selected.use.read() + tail
    });
    assert_eq!(coroutine.as_mut().resume(5), CoroutineState::Yielded(48));
    assert_eq!(coroutine.as_mut().resume(7), CoroutineState::Complete(50));
    assert_eq!(clones.get(), 2);

    let token = Token { clones: clones.clone(), value: 59 };
    let mut future = std::pin::pin!(async move {
        let selected = token.use;
        let first = async { selected.use.read() }.await;
        first + token.read()
    });
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(118));
    assert_eq!(clones.get(), 4);
}
