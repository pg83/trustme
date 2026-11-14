//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: u16

trait Feed {
    type Elem;
}

struct Source<T>(T);

impl<T> Feed for Source<T> {
    type Elem = T;
}

trait Join {
    type Left;
    type Right;
}

impl<S, U> Join for S
where
    S: Feed<Elem = U>,
    U: Feed,
{
    type Left = U;
    type Right = <U as Feed>::Elem;
}

fn demand<T: Join<Left = Source<u8>, Right = u16>>() {}

fn main() {
    demand::<Source<Source<u8>>>();
}
