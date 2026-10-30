#![feature(trait_alias)]
trait Via { type Output; fn via(self) -> Self::Output; }
impl Via for Vec<u8> { type Output = u16; fn via(self) -> u16 { self.len() as u16 + 7 } }
trait Owner { type Item; fn item(self) -> Self::Item; }
struct Holder<T>(T);
impl<T> Owner for Holder<T> { type Item = T; fn item(self) -> T { self.0 } }
trait Routed = Owner<Item: Via>;
trait Route { type Result; fn route(self) -> Self::Result; }
impl<O: Routed> Route for O {
    type Result = <O::Item as Via>::Output;
    fn route(self) -> Self::Result { self.item().via() }
}
fn consume<T: Route<Result = u16>>(value: T) -> u16 { value.route() }
fn main() { assert_eq!(consume(Holder(Vec::new())), 7); }
