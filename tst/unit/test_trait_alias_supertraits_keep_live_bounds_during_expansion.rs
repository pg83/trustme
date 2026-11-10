#![feature(trait_alias)]

trait Parent {
    fn parent(&self) -> u16;
}

trait Child: Parent + Sized {
    fn child(&self) -> u16;
}

trait Required = Child;

struct Value;
impl Parent for Value {
    fn parent(&self) -> u16 {
        7
    }
}

impl Child for Value {
    fn child(&self) -> u16 {
        11
    }
}

fn through<T: Required>(value: &T) -> u16 {
    value.parent() + value.child()
}

fn main() {
    assert_eq!(through(&Value), 18);
}
