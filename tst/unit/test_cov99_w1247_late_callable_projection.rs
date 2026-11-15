//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

type Maybe<T> = Option<T>;
struct Adapter<F>(F);

trait Produce {
    type Value;
}

impl<F, T> Produce for Adapter<F>
where
    F: FnOnce() -> Maybe<T>,
{
    type Value = T;
}

fn receive<P: Produce>(_: P, _: P::Value) {}
fn make<T>() -> T { loop {} }
fn settle<T: Copy>(_: T, _: T) {}
fn output() -> Maybe<&'static u8> { Some(&7u8) }

fn main() {
    let producer = make();
    receive(Adapter(producer), &9u8);
    settle(producer, output as fn() -> Maybe<&'static u8>);
}
