//@ compile-variant: -O
trait Read { type Value; fn read(&self) -> Self::Value; }
trait Provider { type Output: Read<Value = u16>; fn make() -> Self::Output; }
struct Item(u16);
impl Read for Item { type Value = u16; fn read(&self) -> u16 { self.0 } }
struct Source;
impl Provider for Source {
    type Output = Item;
    fn make() -> Item { Item(53) }
}
fn through<P: Provider>() -> u16 { P::make().read() }
fn main() { assert_eq!(through::<Source>(), 53); }
