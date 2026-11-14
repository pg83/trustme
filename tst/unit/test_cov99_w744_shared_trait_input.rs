//@ compile-only
//@ compile-flags: --emit=metadata

trait Marker {}
impl Marker for u8 {}

trait Pick<A, B = A> {
    fn pick() -> A;
}

impl<T> Pick<T, &'static u8> for bool {
    fn pick() -> T {
        loop {}
    }
}

fn main() {
    let value = <bool as Pick<_>>::pick();
    let _: &dyn Marker = value;
}
