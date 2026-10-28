//@ aux-build: assembly_wire_roundtrip.rs

use assembly_wire_roundtrip::{add, address, explicit_register, jump, MARKER};

fn main() {
    let value = std::hint::black_box(29u64);
    assert_eq!(add::<7>(value), 36);
    assert_eq!(add::<19>(value), 48);
    assert_eq!(explicit_register(value), value);
    assert_eq!(jump(value), 42);
    assert_eq!(address(), &MARKER as *const u8);
    assert_eq!(unsafe { *address() }, 37);
}
