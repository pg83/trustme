#![feature(lang_items, no_core)]
#![no_core]

#[lang = "sized"]
trait Sized {}

#[lang = "fn_once"]
pub trait FnOnce<Args> {
    #[lang = "fn_once_output"]
    type Output;

    extern "rust-call" fn call_once(self, args: Args) -> Self::Output;
}

fn takes_fn(a: i32, f: impl FnOnce(i32) -> i32) -> i32 {
    f(a)
}

fn main() -> i32 {
    let capture = 2;
    let add_capture = |value: i32| value + capture;
    let increment = |value: i32| value + 1;
    increment(1) + takes_fn(1, add_capture) - 5
}
