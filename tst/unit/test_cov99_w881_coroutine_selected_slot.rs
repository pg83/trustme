//@ compile-flags: -Z mir-opt-level=2 -Z dump-mir

use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

async fn retained(release: bool, leave: bool) {
    let value = Box::new(9u8);
    if release {
        drop(value);
    }
    std::future::pending::<()>().await;
    if leave {
        return;
    }
    std::future::pending::<()>().await;
}

fn poll_selected<F: Future<Output = ()>>(
    first: &mut F,
    second: &mut F,
    choose_first: bool,
    context: &mut Context<'_>,
) -> Poll<()> {
    let selected = if choose_first { first } else { second };
    unsafe { Pin::new_unchecked(selected) }.poll(context)
}

fn main() {
    let mut first = retained(false, false);
    let mut second = retained(true, true);
    let mut context = Context::from_waker(Waker::noop());
    assert!(poll_selected(&mut first, &mut second, true, &mut context).is_pending());
    assert!(poll_selected(&mut first, &mut second, false, &mut context).is_pending());
}
