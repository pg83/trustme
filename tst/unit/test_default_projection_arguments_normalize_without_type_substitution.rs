#![feature(specialization)]
#![allow(incomplete_features)]

trait Kind {
    type Result;
}

struct Hidden;
impl Kind for Hidden {
    default type Result = u32;
}

trait Map<T> {
    fn mapped(&self) -> u16;
}

trait Holder {
    type Value: Map<<Hidden as Kind>::Result>;
}

struct Value;
impl Map<<Hidden as Kind>::Result> for Value {
    fn mapped(&self) -> u16 {
        29
    }
}

struct Source;
impl Holder for Source {
    type Value = Value;
}

fn read<H: Holder>(value: &H::Value) -> u16 {
    value.mapped()
}

fn main() {
    assert_eq!(read::<Source>(&Value), 29);
}
