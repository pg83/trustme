//@ compile-variant: -O
trait Read { fn read(&self) -> u16; }
trait Provider { type Output: Read; fn make() -> Self::Output; }
struct Item(u16);
impl Read for Item { fn read(&self) -> u16 { self.0 } }
struct Source;
impl Provider for Source {
    type Output = Item;
    fn make() -> Item { Item(67) }
}
fn through<P: Provider>() -> u16 { P::make().read() }
fn main() { assert_eq!(through::<Source>(), 67); }
