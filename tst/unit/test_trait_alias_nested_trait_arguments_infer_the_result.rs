#![feature(trait_alias)]
trait Via<Output> { fn via(self) -> Output; }
impl Via<u16> for u8 { fn via(self) -> u16 { self as u16 + 3 } }
trait Owner { type Item; fn item(self) -> Self::Item; }
trait Routed<Output> = Owner<Item: Via<Output>>;
struct Byte(u8);
impl Owner for Byte { type Item = u8; fn item(self) -> u8 { self.0 } }
trait Route<Output> { fn route(self) -> Output; }
impl<O: Routed<Output>, Output> Route<Output> for O {
    fn route(self) -> Output { self.item().via() }
}
fn main() {
    let value = Byte(9).route();
    assert_eq!(std::mem::size_of_val(&value), 2);
    assert_eq!(value, 12);
}
