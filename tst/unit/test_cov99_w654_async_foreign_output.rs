//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

use core::future::Future;

fn require_u16<F: Future<Output = u16>>(_: F) {}

fn main() {
    let future = async { 37 };
    require_u16(future);
}
