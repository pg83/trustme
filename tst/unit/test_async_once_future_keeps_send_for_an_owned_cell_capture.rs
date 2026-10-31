#![feature(specialization)]
#![allow(incomplete_features)]

use std::cell::Cell;
use std::future::Future;

trait SendFlag { fn flag() -> bool; }
impl<T> SendFlag for T { default fn flag() -> bool { false } }
impl<T: Send> SendFlag for T { fn flag() -> bool { true } }

fn send_flag<T>(_: &T) -> bool { T::flag() }

fn take<F: AsyncFnOnce() -> u16>(call: F) -> impl Future<Output = u16> { call() }

fn main() {
    let value = Cell::new(257u16);
    let future = take(async move || value.get());
    assert!(send_flag(&future));
}
