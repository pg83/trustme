//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Reading<T: Clone> = impl Clone;

#[define_opaque(Reading)]
fn reading<T: Clone>(value: T) -> Reading<T> { value }

fn duplicate<T: Clone>(value: &Reading<T>) -> Reading<T> {
    <Reading<_> as Clone>::clone(value)
}

struct Inspect<T>(T);
impl Inspect<Reading<u8>> {
    fn byte_code(&self) -> u8 { 101 }
}
impl Inspect<Reading<u16>> {
    fn word_code(&self) -> u8 { 103 }
}

fn main() {
    let byte = reading(107u8);
    let word = reading(109u16);
    assert_eq!(Inspect(duplicate(&byte)).byte_code(), 101);
    assert_eq!(Inspect(duplicate(&word)).word_code(), 103);
}
