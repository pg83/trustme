//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(no_core, lang_items, async_drop)]
#![no_core]

#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "legacy_receiver"] pub trait LegacyReceiver: PointeeSized {}
impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}
#[lang = "destruct"] pub trait Destruct {}
#[lang = "copy"] pub trait Copy {}
impl<T: PointeeSized> Copy for *mut T {}
#[lang = "drop"] pub trait Drop { fn drop(&mut self); }
#[lang = "drop_in_place"] pub unsafe fn drop_in_place<T: ?Sized>(_: *mut T) {}

#[lang = "deref"] pub trait Deref: PointeeSized {
    #[lang = "deref_target"] type Target: ?Sized;
    fn deref(&self) -> &Self::Target;
}
#[lang = "pin"] pub struct Pin<P> { pointer: P }
impl<P> Pin<P> {
    pub unsafe fn new_unchecked(pointer: P) -> Self { Pin { pointer } }
}
impl<T: ?Sized> Deref for Pin<&mut T> {
    type Target = T;
    fn deref(&self) -> &T { &*self.pointer }
}
impl<T: ?Sized> LegacyReceiver for Pin<&mut T> {}
#[lang = "Context"] pub struct Context;
#[lang = "ResumeTy"] pub struct ResumeTy(*mut Context);
impl Copy for ResumeTy {}
#[lang = "get_context"] pub unsafe fn get_context<'a>(resume: ResumeTy) -> &'a mut Context {
    &mut *resume.0
}
#[lang = "Poll"] pub enum Poll<T> { Ready(T), Pending }
#[lang = "future_trait"] pub trait Future {
    #[lang = "future_output"] type Output;
    #[lang = "poll"] fn poll(self: Pin<&mut Self>, cx: &mut Context) -> Poll<Self::Output>;
}
#[lang = "async_drop"] pub trait AsyncDrop: Drop {
    async fn drop(self: Pin<&mut Self>);
}
#[lang = "manually_drop"] pub struct ManuallyDrop<T> { value: T }
#[lang = "maybe_uninit"] pub union MaybeUninit<T> { uninit: (), value: ManuallyDrop<T> }
#[lang = "async_drop_in_place"] pub async unsafe fn async_drop_in_place<T: ?Sized>(_: *mut T) {}

impl<T> Drop for ManuallyDrop<T> { fn drop(&mut self) {} }
impl<T> AsyncDrop for ManuallyDrop<T> {
    async fn drop(self: Pin<&mut Self>) {}
}
pub struct Packet { manual: ManuallyDrop<u8> }
impl Drop for Packet { fn drop(&mut self) {} }

pub async fn probe() {
    let _packet = Packet { manual: ManuallyDrop { value: 1u8 } };
}
