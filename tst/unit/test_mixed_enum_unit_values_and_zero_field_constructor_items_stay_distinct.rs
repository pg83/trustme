//@ aux-build: enum_value_path_exports.rs
//@ compile-variant: -O
use enum_value_path_exports::{Packet, NEW_ZERO, NONE};
fn invoke<F: FnOnce() -> Packet<u16>>(function: F) -> Packet<u16> { function() }
fn main() {
    let pointer: fn() -> Packet<u16> = NEW_ZERO;
    assert_eq!(pointer(), Packet::Zero());
    assert_eq!(invoke(NEW_ZERO), Packet::Zero());
    assert_eq!(std::mem::size_of_val(&NEW_ZERO::<u16>), 0);
    assert_eq!(std::mem::size_of_val(&NONE::<u16>), std::mem::size_of::<Packet<u16>>());
    assert_ne!(NONE::<u16>, invoke(pointer));
}
