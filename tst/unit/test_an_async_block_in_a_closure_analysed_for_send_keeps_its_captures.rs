//@ edition: 2021
// tokio's rt_common hands `thread::spawn` a closure that spawns
// `async move { tx.send(()) }`. The closure's `Send` makes type checking
// analyse its captures early, with the usage annotator that runs again
// after type checking; the annotator appends a coroutine's captures
// rather than assigning them, so the async block ended up with every
// capture twice and freed the sender twice.
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

fn block_on<F: Future>(fut: F) -> F::Output {
    let mut fut = pin!(fut);
    let mut cx = Context::from_waker(Waker::noop());
    loop {
        if let Poll::Ready(v) = fut.as_mut().poll(&mut cx) {
            return v;
        }
    }
}

fn need_send<T: Send>(_: &T) {}

fn main() {
    let s = String::from("hello");
    let f = move || block_on(async move { s.len() });
    need_send(&f);
    assert_eq!(f(), 5);
    let v = vec![String::from("a"), String::from("bc")];
    let g = move || {
        let inner = async move { v.iter().map(|x| x.len()).sum::<usize>() };
        block_on(inner)
    };
    need_send(&g);
    assert_eq!(g(), 3);
}
