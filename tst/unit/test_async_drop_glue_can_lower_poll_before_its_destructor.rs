//@ compile-flags: -Cpanic=abort -Clink-arg=-lc
#![feature(no_core, lang_items, arbitrary_self_types, async_drop, rustc_attrs, auto_traits)]
#![no_core]
#![no_main]
#![allow(internal_features, incomplete_features, async_fn_in_trait)]

#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "bikeshed_guaranteed_no_drop"] pub trait NoDrop {}
#[lang = "freeze"] pub unsafe auto trait Freeze {}
#[lang = "copy"] pub trait Copy {}
impl<T: PointeeSized> Copy for *mut T {}
impl Copy for u16 {}
impl Copy for i32 {}

#[lang = "future_trait"]
pub trait PollBeforeCleanup {
    #[lang = "future_output"] type Output;
    #[lang = "poll"]
    fn poll(self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output>;
}

#[lang = "legacy_receiver"] pub trait LegacyReceiver: PointeeSized {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}
#[lang = "receiver"]
pub trait Receiver: PointeeSized {
    #[lang = "receiver_target"] type Target: PointeeSized;
}
impl<T: PointeeSized> Receiver for Pin<&mut T> { type Target = T; }
#[lang = "pin"] pub struct Pin<P> { pub pointer: P }
impl<P> Pin<P> {
    #[lang = "new_unchecked"]
    pub unsafe fn new_unchecked(pointer: P) -> Self { Pin { pointer } }
}
#[lang = "ResumeTy"] pub struct ResumeTy(pub *mut Context);
#[lang = "get_context"]
pub fn get_context<'a>(resume: ResumeTy) -> &'a mut Context { unsafe { &mut *resume.0 } }
#[lang = "manually_drop"]
#[repr(transparent)]
#[rustc_pub_transparent]
pub struct ManuallyDrop<T>(pub T);
#[lang = "maybe_uninit"] pub union MaybeUninit<T> { empty: (), value: ManuallyDrop<T> }
#[lang = "unpin"] pub trait Unpin {}
#[lang = "Context"] pub struct Context;
#[lang = "Poll"] pub enum Poll<T> { Ready(T), Pending }
#[lang = "drop"] pub trait CleanupAfterPoll { fn drop(&mut self); }
#[lang = "async_drop"] pub trait AsyncDrop { async fn drop(self: Pin<&mut Self>); }
#[lang = "destruct"] pub trait Destruct {}
#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_value: *mut T) {}
#[lang = "async_drop_in_place"]
pub async unsafe fn async_drop_in_place<T>(value: *mut T) {
    unsafe { drop_in_place(value) }
}

#[no_mangle]
extern "C" fn main() -> i32 {
    let mut value = 61u16;
    let mut context = Context;
    let mut future = unsafe { async_drop_in_place(&mut value) };
    match PollBeforeCleanup::poll(Pin { pointer: &mut future }, &mut context) {
        Poll::Ready(()) => match value { 61 => 0, _ => 1 },
        Poll::Pending => 2,
    }
}
