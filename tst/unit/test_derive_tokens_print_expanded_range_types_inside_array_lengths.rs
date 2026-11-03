//@ proc-macro-aux-build: observe_const_array_tokens.rs
//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]

#[derive(observe_const_array_tokens::ObserveArray)]
struct Packet {
    bytes: [u8; {
        let bounded: std::pat::pattern_type!(u8 is 3..=9) = 5;
        let raw: u8 = unsafe { std::mem::transmute(bounded) };
        raw as usize
    }],
}

fn main() {
    let packet = Packet { bytes: [23; 5] };
    assert_eq!(std::mem::size_of_val(&packet), 5);
    assert_eq!(packet.bytes[4], 23);
}
