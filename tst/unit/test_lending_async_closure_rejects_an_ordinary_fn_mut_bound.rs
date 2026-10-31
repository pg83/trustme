//@ compile-fail: Failed to find an impl

use std::future::Future;

fn accept<F: FnMut(u8) -> R, R: Future<Output = usize>>(_: F) {}

fn main() {
    let mut values = Vec::new();
    accept(async move |amount| { values.push(amount); values.len() });
}
