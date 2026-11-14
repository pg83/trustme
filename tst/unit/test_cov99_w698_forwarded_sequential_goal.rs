//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: error:

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

trait Choose {}
struct Sink<T>(T);

impl<T: Join<Left = Source<u8>, Right = u16>> Choose for Sink<T> {}

fn demand<T: Choose>() {}

fn main() {
    demand::<Sink<Source<Source<u8>>>>();
}
