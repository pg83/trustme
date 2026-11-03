#![feature(arbitrary_self_types, coerce_unsized, dispatch_from_dyn, unsize)]
use std::rc::Rc;
use std::marker::{PhantomData, Unsize};
use std::ops::{Deref, CoerceUnsized, DispatchFromDyn};
struct Handle<T: ?Sized>(Rc<T>);
impl<T: ?Sized> Deref for Handle<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Handle<U>> for Handle<T> {}
impl<T: ?Sized + Unsize<U>, U: ?Sized> DispatchFromDyn<Handle<U>> for Handle<T> {}
trait Read { fn read(self: Handle<Self>) -> u32; }
impl Read for u32 { fn read(self: Handle<Self>) -> u32 { *self + 6 } }
fn main() {
    let source: Handle<u32> = Handle(Rc::new(251u32));
    let target: Handle<dyn Read> = source;
    assert_eq!(target.read(), 257);
}
