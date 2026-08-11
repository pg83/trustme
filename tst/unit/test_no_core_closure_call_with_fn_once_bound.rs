#![feature(lang_items, no_core, unboxed_closures)]
#![no_core]
#![no_main]
//@ compile-flags: -Cpanic=abort -Clink-arg=-lc

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}

impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "copy"]
trait Copy {}

impl Copy for i32 {}

#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_to_drop: *mut T) {}

#[lang = "destruct"]
pub trait Destruct {}

#[lang = "tuple_trait"]
pub trait Tuple {}

#[lang = "fn_once"]
pub trait FnOnce<Args: Tuple> {
    #[lang = "fn_once_output"]
    type Output;

    extern "rust-call" fn call_once(self, args: Args) -> Self::Output;
}

#[lang = "fn_mut"]
pub trait FnMut<Args: Tuple>: FnOnce<Args> {
    extern "rust-call" fn call_mut(&mut self, args: Args) -> Self::Output;
}

#[lang = "fn"]
pub trait Fn<Args: Tuple>: FnMut<Args> {
    extern "rust-call" fn call(&self, args: Args) -> Self::Output;
}

fn takes_fn(a: i32, f: impl FnOnce(i32) -> i32) -> i32 {
    f(a)
}

struct Capture(i32);

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let capture = Capture(0);
    let return_capture = move |_value: i32| {
        let Capture(value) = capture;
        value
    };
    takes_fn(1, return_capture)
}
