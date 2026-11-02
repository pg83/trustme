//@ compile-variant: -O
#![feature(type_alias_impl_trait)]
type Key = impl Copy + PartialEq + Eq;
#[define_opaque(Key)]
const KEY: Key = 17u8;
struct Record { value: Key }
const SIZE: usize = std::mem::size_of::<Record>();
const ALIGN: usize = std::mem::align_of::<Record>();
fn main() {
    let record = Record { value: KEY };
    assert_eq!((SIZE, ALIGN), (1, 1));
    assert_eq!(std::mem::size_of_val(&record), 1);
    assert!(record.value == KEY);
}
