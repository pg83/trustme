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

fn relate<A, B, Marker>(_: A, _: B, _: Marker)
where
    A: Link<B, Marker>,
{}

fn main() {
    let (first, second) = pair();
    relate(left(first), right(second), 0);
}
