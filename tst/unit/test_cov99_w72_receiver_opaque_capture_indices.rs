//@ compile-fail: error:

trait Link<Right, Marker> {}
impl<T, Marker> Link<T, Marker> for T {}

fn pair() -> (impl Copy, impl Copy) {
    (1u8, 2u16)
}

fn left<T>(_: T) -> impl Copy + use<T> {
    3u32
}

fn right<T>(_: T) -> impl Copy + use<T> {
    4u64
}

struct Both<L, R>(L, R);
impl<L, R> Both<L, R> {
    fn relate<Marker>(self, _: Marker)
    where
        L: Link<R, Marker>,
    {}
}

fn main() {
    let (first, second) = pair();
    let values = Both(left(first), right(second));
    values.relate(0);
}
