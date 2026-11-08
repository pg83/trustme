#![feature(type_ascription, builtin_syntax, ergonomic_clones)]

use std::future::Future;
use std::task::{Context, Poll, Waker};

fn checked() -> Result<(), u8> {
    assert!(if Ok::<bool, u8>(true)? { true } else { false });
    assert!(if true.use { true } else { false });
    assert!(if builtin # type_ascribe(true, bool) { true } else { false });
    Ok(())
}

async fn awaited() {
    assert!(if std::future::ready(true).await { true } else { false });
}

fn main() {
    assert_eq!(checked(), Ok(()));
    let mut future = std::pin::pin!(awaited());
    let mut context = Context::from_waker(Waker::noop());
    assert!(matches!(future.as_mut().poll(&mut context), Poll::Ready(())));
}
