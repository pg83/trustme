type First = u8;
type Second = u16;

trait Container<T> {
    type Item<U>;
    fn join<U>(self, first: T, second: U) -> Self::Item<U>;
}
trait Pair<A, B> { fn pair(self) -> (A, B); }
impl<T> Container<T> for () {
    type Item<U> = (T, U);
    fn join<U>(self, first: T, second: U) -> (T, U) { (first, second) }
}
impl<A, B> Pair<A, B> for (A, B) {
    fn pair(self) -> (A, B) { self }
}

fn source() -> impl Container<First, Item<Second>: Pair<First, Second>> { () }
fn unpack<C>(source: C) -> (First, Second)
where C: Container<First, Item<Second>: Pair<First, Second>> {
    source.join(13, 37).pair()
}

fn main() { assert_eq!(unpack(source()), (13, 37)); }
