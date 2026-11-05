//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Pair = (impl Copy, impl Copy);
#[define_opaque(Pair)]
fn make() -> Pair { (83u8, 89u8) }

trait Parts { type First; type Second; }
impl<A, B> Parts for (A, B) { type First = A; type Second = B; }
type First = <Pair as Parts>::First;
type Second = <Pair as Parts>::Second;

struct Tagged<T>(T);
impl Tagged<First> { fn first(&self) -> u8 { 97 } }
impl Tagged<Second> { fn second(&self) -> u8 { 101 } }

fn main() {
    let pair = make();
    assert_eq!(Tagged(pair.0).first(), 97);
    assert_eq!(Tagged(pair.1).second(), 101);
}
