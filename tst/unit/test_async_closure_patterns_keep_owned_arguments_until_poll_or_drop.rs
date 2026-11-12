use std::cell::Cell;
use std::future::Future;
use std::rc::Rc;
use std::task::{Context, Poll, Waker};

struct Token(u16, Rc<Cell<usize>>);
impl Drop for Token {
    fn drop(&mut self) { self.1.set(self.1.get() + 1); }
}

fn ready<F: Future>(future: F) -> F::Output {
    let mut future = std::pin::pin!(future);
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => value,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let drops = Rc::new(Cell::new(0));
    let read = async |(ref value, _): (Token, Token)| value.0;
    let future = read((Token(31, drops.clone()), Token(37, drops.clone())));
    assert_eq!(drops.get(), 0);
    drop(future);
    assert_eq!(drops.get(), 2);

    let mut total = 0;
    {
        let mut add = async |(ref value, _): (Token, Token)| {
            total += value.0;
            total
        };
        let future = add((Token(43, drops.clone()), Token(47, drops.clone())));
        assert_eq!(drops.get(), 2);
        assert_eq!(ready(future), 43);
        assert_eq!(drops.get(), 4);
    }
    assert_eq!(total, 43);

    let captured = String::from("oak");
    let consume = async move |(ref value, _): (Token, Token)| {
        drop(captured);
        value.0
    };
    let future = consume((Token(53, drops.clone()), Token(59, drops.clone())));
    assert_eq!(drops.get(), 4);
    assert_eq!(ready(future), 53);
    assert_eq!(drops.get(), 6);
}
