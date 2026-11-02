//@ compile-variant: -O
#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
trait Base { fn base(&self) -> u16; }
trait Derived: Base { fn derived(&self) -> u16; }
struct Number(u16);
impl Base for Number { fn base(&self) -> u16 { self.0 } }
impl Derived for Number { fn derived(&self) -> u16 { self.0 + 6 } }
const fn widen<P: CoerceUnsized<Q>, Q>(pointer: P) -> Q { pointer }
static NUMBER: Number = Number(17);
const DERIVED: &dyn Derived = widen(&NUMBER);
const BASE: &dyn Base = widen(DERIVED);
fn main() {
    assert_eq!(DERIVED.derived(), 23);
    assert_eq!(BASE.base(), 17);
    assert_eq!(std::mem::size_of_val(BASE), 2);
}
