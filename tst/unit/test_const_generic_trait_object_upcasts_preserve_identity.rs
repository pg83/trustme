#![feature(unsize)]

use std::marker::Unsize;

trait Parent {
    fn value(&self) -> u8;
}

trait Child: Parent {}

struct Token;
impl Parent for Token {
    fn value(&self) -> u8 { 37 }
}
impl Child for Token {}

static TOKEN: Token = Token;

const fn coerce<T: ?Sized + Unsize<U>, U: ?Sized>(value: &T) -> &U {
    value
}

const CHILD: &dyn Child = &TOKEN;
const PARENT: &dyn Parent = coerce(CHILD);
const SAME: &dyn Parent = coerce(PARENT);

fn main() {
    assert_eq!(PARENT.value(), 37);
    assert_eq!(SAME.value(), 37);
    assert!(std::ptr::eq(PARENT, SAME));
}
