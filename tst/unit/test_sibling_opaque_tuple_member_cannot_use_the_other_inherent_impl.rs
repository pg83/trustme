//@ compile-fail: No applicable methods
#![feature(type_alias_impl_trait)]

type Pair = (impl Copy, impl Copy);
#[define_opaque(Pair)]
fn make() -> Pair { (103u8, 107u8) }

trait Parts { type First; type Second; }
impl<A, B> Parts for (A, B) { type First = A; type Second = B; }
type First = <Pair as Parts>::First;

struct Tagged<T>(T);
impl Tagged<First> { fn first(&self) -> u8 { 109 } }

fn main() {
    let pair = make();
    let _ = Tagged(pair.1).first();
}
