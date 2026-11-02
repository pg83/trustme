//@ compile-variant: -O
#![allow(non_upper_case_globals)]

struct Slot<T>(std::marker::PhantomData<T>);
fn add(value: u16) -> u16 { value + 3 }
impl Slot<u8> { const select: fn(u16) -> u16 = add; }
impl Slot<u16> { fn select(value: u16) -> u16 { value + 7 } }

fn main() {
    let constant = Slot::<u8>::select;
    let function = Slot::<u16>::select;
    assert_eq!(constant(11), 14);
    assert_eq!(function(11), 18);
    assert_eq!(Slot::<u8>::select(19), 22);
    assert_eq!(Slot::<u16>::select(19), 26);
}
