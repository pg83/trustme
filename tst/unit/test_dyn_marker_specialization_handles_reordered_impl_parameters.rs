#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Family<T> {}
struct Holder<X, Y>(PhantomData<(X, Y)>);
struct NotClone;
trait Score { const ID: u8; fn code() -> u8; }
impl<A> Score for Holder<u32, &(dyn Family<A> + Send + Sync)> { const ID: u8 = 7; }
impl<B: Clone, A> Score for Holder<B, &(dyn Family<A> + Send + Sync)> {
    default const ID: u8 = 2;
    default fn code() -> u8 { Self::ID + 20 }
}
impl<A, B> Score for Holder<B, &(dyn Family<A> + Send + Sync)> {
    default const ID: u8 = 1;
    default fn code() -> u8 { Self::ID + 10 }
}

fn main() {
    type Object = dyn Family<u8> + Send + Sync;
    assert_eq!(<Holder<u32, &Object> as Score>::code(), 27);
    assert_eq!(<Holder<u16, &Object> as Score>::code(), 22);
    assert_eq!(<Holder<NotClone, &Object> as Score>::code(), 11);
}
