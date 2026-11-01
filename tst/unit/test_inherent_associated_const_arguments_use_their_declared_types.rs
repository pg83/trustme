#![feature(inherent_associated_types)]

struct Select<T>(T);
struct Byte<const VALUE: u8>;
struct Flag<const VALUE: bool>;
impl<const VALUE: u8> Byte<VALUE> { fn value(&self) -> u8 { VALUE } }
impl<const VALUE: bool> Flag<VALUE> { fn value(&self) -> bool { VALUE } }
impl<T> Select<T> {
    type Byte<const VALUE: u8> = (T, Byte<VALUE>);
    type Flag<const VALUE: bool> = (T, Flag<VALUE>);
    fn byte(value: T) -> Self::Byte<3> { (value, Byte) }
    fn flag(value: T) -> Self::Flag<true> { (value, Flag) }
}

fn main() {
    let (value, bytes) = Select::byte(19u16);
    assert_eq!(value, 19);
    assert_eq!(bytes.value(), 3);
    let (value, flags) = Select::flag(23u32);
    assert_eq!(value, 23);
    assert!(flags.value());
}
