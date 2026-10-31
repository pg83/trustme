#![feature(auto_traits)]
macro_rules! items {
    ($($item:item)*) => { $($item)* };
}
macro_rules! auto { () => { const MACRO_VALUE: u8 = 29; }; }
macro_rules! forward_item { ($item:item) => { items! { $item } }; }
forward_item!(fn forwarded() -> u8 { 43 });
items! {
    use std::num::Wrapping;
    extern crate self as this_crate;
    auto!();
    auto trait Marker {}
    union Bits { word: u32, bytes: [u8; 4] }
    struct Packet<T>(T);
    trait Read { fn read(&self) -> u8; }
    impl Read for Packet<u8> { fn read(&self) -> u8 { self.0 } }
    const unsafe fn offset(value: u8) -> u8 { value + 3 }
    unsafe extern "C" fn double(value: u8) -> u8 { value * 2 }
}
fn accepts_marker<T: Marker>(_: &T) {}
fn main() {
    assert_eq!(MACRO_VALUE, 29);
    assert_eq!(forwarded(), 43);
    let value = Packet(37u8);
    accepts_marker(&value);
    assert_eq!(value.read(), 37);
    assert_eq!(Wrapping(250u8) + Wrapping(10), Wrapping(4));
    assert_eq!(unsafe { offset(value.read()) }, 40);
    assert_eq!(unsafe { double(17) }, 34);
    let bits = Bits { word: 0 };
    assert_eq!(unsafe { bits.bytes }, [0; 4]);
    let other: this_crate::Packet<u8> = Packet(19);
    assert_eq!(other.read(), 19);
}
