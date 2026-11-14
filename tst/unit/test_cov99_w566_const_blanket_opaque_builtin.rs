//@ crate-type: lib
//@ compile-flags: --emit=metadata -Z dump-hir
#![feature(no_core, lang_items, unboxed_closures, type_alias_impl_trait, rustc_attrs)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}
#[lang = "tuple_trait"]
pub trait Tuple {}
#[lang = "future_trait"]
pub trait Future {
    type Output;
}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver {}
impl<T: ?Sized> LegacyReceiver for &T {}
impl<T: ?Sized> LegacyReceiver for &mut T {}

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

pub trait ArrayShape {
    type Array;
}
impl<T> ArrayShape for T {
    type Array = [u8; 1];
}
impl<T, const N: usize> FnOnce<()> for T
where
    T: ArrayShape<Array = [u8; N]>,
{
    type Output = [u8; N];
    extern "rust-call" fn call_once(self, _: ()) -> Self::Output {
        loop {}
    }
}

#[rustc_builtin_macro]
macro_rules! define_opaque { ($($token:tt)*) => {}; }

pub type Callback = impl ArrayShape;

struct Sink;
impl Sink {
    fn accept(&self, callback: Callback) -> Callback {
        callback
    }
}

#[define_opaque(Callback)]
pub fn make() -> Callback {
    Sink.accept(|| [0u8])
}
