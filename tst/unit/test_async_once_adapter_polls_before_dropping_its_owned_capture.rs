use std::cell::Cell;
use std::future::Future;
use std::rc::Rc;
use std::task::{Context, Poll, Waker};

struct Token { value: u16, drops: Rc<Cell<usize>> }
impl Token {
    fn add(&mut self, amount: u8) -> u16 {
        assert_eq!(self.drops.get(), 0);
        self.value += u16::from(amount);
        self.value
    }
}
impl Drop for Token {
    fn drop(&mut self) { self.drops.set(self.drops.get() + 1); }
}

fn take<F: AsyncFnOnce(u8) -> u16>(call: F, value: u8) -> impl Future<Output = u16> {
    call(value)
}

fn main() {
    let drops = Rc::new(Cell::new(0));
    let mut token = Token { value: 257, drops: drops.clone() };
    let future = take(async move |amount| token.add(amount), 7);
    assert_eq!(drops.get(), 0);
    {
        let mut future = std::pin::pin!(future);
        match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
            Poll::Ready(value) => assert_eq!(value, 264),
            Poll::Pending => panic!("immediate closure suspended"),
        }
    }
    assert_eq!(drops.get(), 1);
}
