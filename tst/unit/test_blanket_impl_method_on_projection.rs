// A method from a blanket impl has to be found on a receiver whose type is an
// associated-type projection. The impl's condition (`F: Future`) is met by
// the projection's own declared bound, so the candidate applies without the
// projection ever being normalized to a concrete type.

trait Future {
    type Item;

    fn tag(&self) -> u8;
}

trait IntoFuture {
    type Out;

    fn into_future(self) -> Self::Out;
}

impl<F: Future> IntoFuture for F {
    type Out = F;

    fn into_future(self) -> F {
        self
    }
}

trait FromRequest {
    type Fut: Future<Item = u8>;

    fn make() -> Self::Fut;
}

fn discarded<I: FromRequest>() -> u8 {
    let _ = I::make().into_future();
    7
}

fn used<I: FromRequest>() -> u8 {
    let future = I::make().into_future();
    future.tag()
}

struct A;

struct F1;

impl Future for F1 {
    type Item = u8;

    fn tag(&self) -> u8 {
        9
    }
}

impl FromRequest for A {
    type Fut = F1;

    fn make() -> F1 {
        F1
    }
}

fn main() {
    assert_eq!(discarded::<A>(), 7);
    assert_eq!(used::<A>(), 9);
}
