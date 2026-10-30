#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : $output:literal($dst:expr) : $input:literal($src:expr) : : $flag:literal) => {{
        $dst = $src;
        core::arch::asm!("/* {value:e} */", value = inout(reg) $dst, options(nomem, nostack));
    }};
}
use std::future::{poll_fn, Future};
use std::pin::pin;
use std::task::{Context, Poll, Waker};
async fn values() -> (u32, u32) {
    let first = std::hint::black_box(43u32);
    let second = std::hint::black_box(47u32);
    let mut value = std::hint::black_box(7u32);
    std::hint::black_box(value);
    unsafe { llvm_asm!("" : "=r"(value) : "0"(first) : : "volatile"); }
    let saved = value;
    let mut ready = false;
    poll_fn(|context| {
        if ready { Poll::Ready(()) } else {
            ready = true;
            context.waker().wake_by_ref();
            Poll::Pending
        }
    }).await;
    unsafe { llvm_asm!("" : "=r"(value) : "0"(second) : : "volatile"); }
    (saved, value)
}
fn main() {
    let mut future = pin!(values());
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready((43, 47)));
}
