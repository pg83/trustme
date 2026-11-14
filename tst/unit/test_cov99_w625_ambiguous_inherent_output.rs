//@ compile-fail: multiple applicable items in scope
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

struct Catalog;
impl Catalog {
    type Value = u8;
}

struct Reader;
trait First {
    fn read(&self) -> Catalog::Value { 29 }
}
trait Second {
    fn read(&self) -> Catalog::Value { 31 }
}
impl First for Reader {}
impl Second for Reader {}

fn main() {
    let _value = Reader.read();
}
