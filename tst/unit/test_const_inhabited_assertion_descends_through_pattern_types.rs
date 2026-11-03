#![feature(core_intrinsics, pattern_types, pattern_type_macro)]

type Window = std::pat::pattern_type!(u16 is 3..=11);
struct Packet { window: Window, count: u8 }

const CHECK: () = {
    core::intrinsics::assert_inhabited::<Window>();
    core::intrinsics::assert_inhabited::<Packet>();
    core::intrinsics::assert_inhabited::<(Window, [Window; 2])>();
};

fn main() {
    let packet = Packet { window: 7, count: 2 };
    let base: u16 = unsafe { std::mem::transmute(packet.window) };
    assert_eq!(base + u16::from(packet.count), 9);
    assert_eq!(CHECK, ());
}
