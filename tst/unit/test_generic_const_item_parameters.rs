#![feature(generic_const_items)]
#![allow(incomplete_features, dead_code)]

const NONE<T>: Option<T> = None;

trait Trait {
    const NONE<T>: Option<T> = None;
}

impl Trait for () {
    const NONE<T>: Option<T> = None;
}

fn main() {
    let _: Option<u8> = NONE::<u8>;
    let _: Option<u16> = <() as Trait>::NONE::<u16>;
}
