//@ compile-variant: -O
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll, Wake, Waker};

struct Gate(u8);

impl Future for Gate {
    type Output = bool;
    fn poll(mut self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<bool> {
        match self.0 {
            0 => {
                self.0 = 1;
                Poll::Pending
            }
            1 => Poll::Ready(true),
            _ => Poll::Ready(false),
        }
    }
}

async fn choose(tag: u8) -> u16 {
    match tag {
        1 | 2 if Gate(0).await => 103,
        3 | 4 if Gate(2).await => 109,
        _ => 107,
    }
}

struct Noop;
impl Wake for Noop {
    fn wake(self: Arc<Self>) {}
}

fn evaluate(tag: u8) -> (u16, u8) {
    let waker = Waker::from(Arc::new(Noop));
    let mut context = Context::from_waker(&waker);
    let mut future = Box::pin(choose(tag));
    let mut polls = 0;
    loop {
        polls += 1;
        assert!(polls <= 2);
        if let Poll::Ready(value) = future.as_mut().poll(&mut context) {
            return (value, polls);
        }
    }
}

fn main() {
    assert_eq!(evaluate(1), (103, 2));
    assert_eq!(evaluate(2), (103, 2));
    assert_eq!(evaluate(3), (107, 1));
    assert_eq!(evaluate(4), (107, 1));
    assert_eq!(evaluate(5), (107, 1));
}
