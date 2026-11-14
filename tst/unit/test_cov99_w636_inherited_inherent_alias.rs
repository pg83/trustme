//@ compile-only
//@ compile-flags: -Z stop-after=typeck
#![feature(inherent_associated_types)]

trait Define {
    type Item;
}
impl Define for () {
    type Item = u16;
}

struct Holder<T>(T);
impl<T, U> Holder<T>
where
    T: Define<Item = U>,
{
    type Alias = U;
}

trait Parent<T> {
    type Item;
}
trait Child: Parent<Holder<()>::Alias> {}
impl Parent<u16> for u8 {
    type Item = ();
}
impl Child for u8 {}

struct Runner;
impl Runner {
    fn check<T: Child<Item = ()>>(&self, _: &T) {}
}

fn main() {
    let value = Default::default();
    Runner.check(&value);
    let _: u8 = value;
}
