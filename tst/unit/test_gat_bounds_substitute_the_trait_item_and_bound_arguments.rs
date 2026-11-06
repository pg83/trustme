trait Container<T> { type Item<U>; }
trait Pair<A, B> { fn pair(self) -> (A, B); }

impl<T> Container<T> for () { type Item<U> = (T, U); }
impl<A, B> Pair<A, B> for (A, B) {
    fn pair(self) -> (A, B) { self }
}

fn unpack<T, U, C>(value: C::Item<U>) -> (T, U)
where C: Container<T, Item<U>: Pair<T, U>> {
    value.pair()
}

fn main() {
    assert_eq!(unpack::<u8, u16, ()>((7, 29)), (7, 29));
    assert_eq!(unpack::<bool, u32, ()>((true, 41)), (true, 41));
}
