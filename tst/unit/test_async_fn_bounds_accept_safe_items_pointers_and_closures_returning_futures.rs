use std::future::Future;
fn ready(value: u32) -> std::future::Ready<u32> { std::future::ready(value + 3) }
fn run<F: std::ops::AsyncFnOnce(u32) -> u32>(callback: F) -> u32 {
    let mut future = std::pin::pin!(callback(151));
    let mut context = std::task::Context::from_waker(std::task::Waker::noop());
    match future.as_mut().poll(&mut context) {
        std::task::Poll::Ready(value) => value,
        std::task::Poll::Pending => panic!("ready future suspended"),
    }
}
fn main() {
    assert_eq!(run(ready), 154);
    assert_eq!(run(ready as fn(u32) -> std::future::Ready<u32>), 154);
    assert_eq!(run(|value| std::future::ready(value + 5)), 156);
}
