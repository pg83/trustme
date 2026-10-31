//@ compile-fail: Failed to find an impl

use std::future::Future;

fn accept<F: FnMut() -> R, R: Future>(_: F) {}

fn main() {
    let value = 257u16;
    let reference = &value;
    accept(async move || &reference);
}
