//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Pair<T: Copy> = (impl Copy, impl Copy);

#[define_opaque(Pair)]
fn make<T: Copy>(value: T) -> Pair<T> { ([value; 2], [value; 3]) }

fn main() {
    let bytes = make(61u8);
    let words = make(67u16);
    assert_eq!(std::mem::size_of_val(&bytes.0), 2);
    assert_eq!(std::mem::size_of_val(&bytes.1), 3);
    assert_eq!(std::mem::size_of_val(&words.0), 4);
    assert_eq!(std::mem::size_of_val(&words.1), 6);
}
