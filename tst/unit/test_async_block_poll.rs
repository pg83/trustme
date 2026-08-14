use std::future::Future;
use std::pin::Pin;
use std::task::Context;

fn check(cx: &mut Context<'_>) {
    let mut future = async {};
    let mut future = unsafe { Pin::new_unchecked(&mut future) };
    let _ = future.as_mut().poll(cx);
}

fn main() {
    let _ = check;
}
