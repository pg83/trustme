//@ compile-variant: -O
trait Read<T> { fn read(&self) -> T; }
trait Provider<T> { type Output: Read<T>; fn make() -> Self::Output; }
struct Item(u16);
impl Read<u16> for Item { fn read(&self) -> u16 { self.0 } }
struct Source;
impl Provider<u16> for Source {
    type Output = Item;
    fn make() -> Item { Item(41) }
}
fn through<T, P: Provider<T>>() -> T { P::make().read() }
fn main() { assert_eq!(through::<u16, Source>(), 41); }
