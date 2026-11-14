//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::future::Future;

trait Family {
    type Item;
}

impl Family for u16 {
    type Item = u8;
}

fn pair<T: Family>() -> (T::Item, T) {
    loop {}
}

fn require_output<F: Future<Output = (u8, u16)>>(_: F) {}

pub fn infer_async_output() {
    let future = async { pair() };
    require_output(future);
}
