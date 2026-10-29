//@ compile-flags: -Cpanic=abort -Clink-arg=-lc
#![feature(no_core, lang_items, arbitrary_self_types, async_drop, rustc_attrs, auto_traits)]
#![no_core]
#![no_main]
#![allow(internal_features, incomplete_features, async_fn_in_trait)]
#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "bikeshed_guaranteed_no_drop"]
pub trait NoDrop {}
#[lang = "freeze"]
pub unsafe auto trait Freeze {}
#[lang = "copy"]
pub trait Copy {}
impl<T: PointeeSized> Copy for *mut T {}
#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}
#[lang = "receiver"]
pub trait Receiver: PointeeSized {
    #[lang = "receiver_target"]
    type Target: PointeeSized;
}
impl<T: PointeeSized> Receiver for Pin<&mut T> { type Target = T; }
#[lang = "async_drop"]
pub trait AsyncDrop { async fn drop(self: Pin<&mut Self>); }
#[lang = "pin"]
pub struct Pin<P> { pub pointer: P }
impl<P> Pin<P> {
    #[lang = "new_unchecked"]
    pub unsafe fn new_unchecked(pointer: P) -> Self { Pin { pointer } }
}
#[lang = "ResumeTy"]
pub struct ResumeTy(pub *mut Context);
#[lang = "get_context"]
pub fn get_context<'a>(resume: ResumeTy) -> &'a mut Context { unsafe { &mut *resume.0 } }
#[lang = "manually_drop"]
#[repr(transparent)]
#[rustc_pub_transparent]
pub struct ManuallyDrop<T>(pub T);
#[lang = "maybe_uninit"]
pub union MaybeUninit<T> { empty: (), value: ManuallyDrop<T> }
#[lang = "drop"]
pub trait Drop { fn drop(&mut self); }
#[lang = "unpin"]
pub trait Unpin {}
#[lang = "Context"]
pub struct Context;
#[lang = "Poll"]
pub enum Poll<T> { Ready(T), Pending }
#[lang = "future_trait"]
pub trait Future {
    #[lang = "future_output"]
    type Output;
    #[lang = "poll"]
    fn poll(self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output>;
}
#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_value: *mut T) {}
#[lang = "destruct"]
pub trait Destruct {}
#[lang = "async_drop_in_place"]
pub async unsafe fn async_drop_in_place<T>(value: *mut T) {
    unsafe { drop_in_place(value) }
}
pub unsafe fn poll_byte(value: *mut u8, context: &mut Context) -> Poll<()> {
    let mut future = unsafe { async_drop_in_place(value) };
    Future::poll(Pin { pointer: &mut future }, context)
}

#[no_mangle]
extern "C" fn main() -> i32 {
    let mut value = 7u8;
    let mut context = Context;
    match unsafe { poll_byte(&mut value, &mut context) } {
        Poll::Ready(()) => 0,
        Poll::Pending => 1,
    }
}
