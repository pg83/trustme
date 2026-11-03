trait Element { type Item; }
struct Wrap<T>(T);
impl<T> Element for Wrap<T> { type Item = T; }
fn require<A: Element<Item = B>, B>(left: A, right: B) -> usize {
    std::mem::size_of::<A>() + std::mem::size_of_val(&right) + std::mem::size_of_val(&left)
}
fn first() -> impl Copy { 83u32 }
struct Factory;
impl Factory { fn second() -> impl Copy { 89u32 } }
trait Source { fn make() -> impl Copy; }
impl Source for Factory { fn make() -> impl Copy { 97u32 } }
fn same_source<T: Source>() -> usize { require(Wrap(T::make()), T::make()) }
fn main() {
    assert_eq!(require(Wrap(first()), first()), 12);
    assert_eq!(require(Wrap(Factory::second()), Factory::second()), 12);
    assert_eq!(same_source::<Factory>(), 12);
}
