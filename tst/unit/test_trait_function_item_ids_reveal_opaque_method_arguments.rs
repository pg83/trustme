//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

use std::any::TypeId;
use std::marker::PhantomData;

type Handler = impl Fn() -> u32 + Copy;

fn answer() -> u32 { 83 }

#[define_opaque(Handler)]
fn make_handler() -> Handler { answer as fn() -> u32 }

struct Holder<T>(PhantomData<T>);

trait Factory<Q> {
    fn method<U>() -> usize;
}

impl<T, Q> Factory<Q> for Holder<T> {
    fn method<U>() -> usize { std::mem::size_of::<(T, Q, U)>() }
}

#[inline(never)]
fn identity<F: 'static>(_: F) -> TypeId { TypeId::of::<F>() }

fn main() {
    assert_eq!(make_handler()(), 83);
    let hidden = <Holder<Handler> as Factory<Handler>>::method::<Handler>;
    let concrete = <Holder<fn() -> u32> as Factory<fn() -> u32>>::method::<fn() -> u32>;
    assert_eq!(hidden(), concrete());
    assert_eq!(identity(hidden), identity(hidden));
    assert_eq!(identity(concrete), identity(concrete));
    assert_ne!(identity(<Holder<Handler> as Factory<Handler>>::method::<u8>),
               identity(<Holder<Handler> as Factory<Handler>>::method::<u16>));
}
