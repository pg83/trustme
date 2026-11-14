//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;

type Callable<T> = impl FnOnce(u8) -> T;

trait Route {
    type Destination;
}

struct Source<T>(PhantomData<T>);
struct Sink<T>(PhantomData<T>);

impl<T> Route for Source<T> {
    type Destination = Sink<T>;
}

impl<T> Sink<T> {
    fn take(f: Callable<T>) -> T {
        f(5)
    }
}

#[define_opaque(Callable)]
fn drive<T>(value: T) -> T {
    <Source<_> as Route>::Destination::take(move |_| value)
}

fn main() {
    assert_eq!(drive(17u32), 17);
}
