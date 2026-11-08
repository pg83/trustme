//@ aux-build: alias_trait_scope_diamond.rs
#![feature(trait_alias)]

struct Number(u16);

impl alias_trait_scope_diamond::Read for Number {
    fn read(&self) -> u16 { self.0 }
}

mod view {
    use alias_trait_scope_diamond::Diamond;

    pub fn value(number: &super::Number) -> u16 {
        number.read()
    }
}

fn main() {
    assert_eq!(view::value(&Number(37)), 37);
}
