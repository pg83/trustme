const UNIT: () = ();
const EMPTY: [u8; 0] = [];

async fn accepts(UNIT: (), EMPTY: [u8; 0]) -> u8 { 11 }

fn main() {
    let call = |UNIT: (), EMPTY: [u8; 0]| 7;
    assert_eq!(call((), []), 7);
    let mut future = std::pin::pin!(accepts((), []));
    let mut context = std::task::Context::from_waker(std::task::Waker::noop());
    assert_eq!(std::future::Future::poll(future.as_mut(), &mut context), std::task::Poll::Ready(11));
}
