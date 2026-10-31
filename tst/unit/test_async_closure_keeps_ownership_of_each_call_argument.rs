use std::cell::Cell;
use std::future::Future;
use std::rc::Rc;
use std::task::{Context, Poll, Waker};

struct Token { id: u16, drops: Rc<Cell<usize>> }
impl Drop for Token {
    fn drop(&mut self) { self.drops.set(self.drops.get() + 1); }
}

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let drops = Rc::new(Cell::new(0));
    let call = async |value: Token| value.id;
    let future = call(Token { id: 257, drops: drops.clone() });
    assert_eq!(drops.get(), 0);
    drop(future);
    assert_eq!(drops.get(), 1);
    assert_eq!(ready(call(Token { id: 263, drops: drops.clone() })), 263);
    assert_eq!(drops.get(), 2);
}
