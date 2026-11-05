//@ compile-fail: error:
#![feature(type_alias_impl_trait)]

type Pair = (impl Copy, impl Clone);

#[define_opaque(Pair)]
fn make() -> Pair { (47u8, 53u16) }

trait Same<R, U> {}
impl<T, U> Same<T, U> for T {}

fn require<A, B, U>(_: A, _: B, tag: U) -> U
where A: Same<B, U>
{
    tag
}

fn main() {
    let pair = make();
    let tag = require(pair.0, pair.1, 7);
    assert_eq!(tag, 7);
}
