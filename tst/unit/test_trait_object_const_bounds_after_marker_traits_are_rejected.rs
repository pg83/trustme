//@ compile-fail: error:

#![feature(const_trait_impl)]

const trait Measure<'a> {
    fn measure(&self) -> u16;
}

type Object = dyn Send + for<'a> [const] Measure<'a>;

fn main() {}
