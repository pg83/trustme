use std::cell::Cell;
use std::future::Future;
use std::rc::Rc;
use std::task::{Context, Poll, Waker};

struct Token { value: u16, drops: Rc<Cell<usize>> }
impl Drop for Token {
    fn drop(&mut self) { self.drops.set(self.drops.get() + 1); }
}

fn main() {
    let drops = Rc::new(Cell::new(0));
    let captured = Token { value: 257, drops: drops.clone() };
    let call = async move |argument: Token| {
        drop(captured);
        argument.value
    };
    let future = call(Token { value: 263, drops: drops.clone() });
    assert_eq!(drops.get(), 0);
    {
        let mut future = std::pin::pin!(future);
        match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
            Poll::Ready(result) => assert_eq!(result, 263),
            Poll::Pending => panic!("immediate closure suspended"),
        }
    }
    assert_eq!(drops.get(), 2);
}
