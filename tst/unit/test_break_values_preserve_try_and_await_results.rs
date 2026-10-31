use core::future::Future;
use core::task::{Context, Poll, Waker};
macro_rules! check_try {
    ($value:expr) => { assert!((loop { break $value? }) == 13) };
}
macro_rules! check_await {
    ($value:expr) => { assert!((loop { break $value.await }) == 21) };
}
fn checked() -> Result<(), ()> {
    check_try!('value: { Ok::<u16, ()>(13) });
    Ok(())
}
async fn checked_async() {
    check_await!('value: { core::future::ready(21_u16) });
}
fn main() {
    assert_eq!(checked(), Ok(()));
    let mut future = core::pin::pin!(checked_async());
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
}
