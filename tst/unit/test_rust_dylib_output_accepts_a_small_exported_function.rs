//@ crate-type: lib
//@ compile-only
//@ compile-variant: --crate-type=dylib

const fn checksum(bytes: &[u8]) -> u32 {
    let mut value = 0u32;
    let mut index = 0;
    while index < bytes.len() {
        value = value * 17 + bytes[index] as u32;
        index += 1;
    }
    value
}
const CHECK: u32 = checksum(&[2, 4, 6]);
fn main() {
    assert_eq!(CHECK, 652);
    assert_eq!(checksum(&[7, 3]), 122);
}

#[no_mangle]
pub extern "C" fn exported_checksum(a: u8, b: u8) -> u32 { checksum(&[a, b]) }
