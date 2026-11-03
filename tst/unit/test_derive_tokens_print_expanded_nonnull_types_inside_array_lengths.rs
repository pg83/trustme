//@ compile-fail: Unexpected token TOK_RWORD_AS
//@ proc-macro-aux-build: observe_const_array_tokens.rs
#![feature(pattern_types, pattern_type_macro)]

static BYTE: u8 = 9;

#[derive(observe_const_array_tokens::ObserveArray)]
struct Packet {
    bytes: [u8; {
        let pointer: std::pat::pattern_type!(*const u8 is !null) =
            unsafe { std::mem::transmute(&BYTE as *const u8) };
        let raw: *const u8 = unsafe { std::mem::transmute(pointer) };
        unsafe { *raw } as usize
    }],
}

fn main() {
    let packet = Packet { bytes: [31; 9] };
    assert_eq!(std::mem::size_of_val(&packet), 9);
    assert_eq!(packet.bytes[8], 31);
}
