//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

type Maybe<T> = Option<T>;

trait Produce {
    type Value;
}

impl<F, T> Produce for F
where
    F: FnOnce() -> Maybe<T>,
{
    type Value = T;
}

fn receive<P: Produce>(_: P, _: P::Value) {}

fn main() {
    let producer = || -> Maybe<_> { Some(&7u8) };
    receive(producer, &9u8);
}
