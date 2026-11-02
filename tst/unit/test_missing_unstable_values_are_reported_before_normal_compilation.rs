//@ reject-flags: -Zmir-opt-level => Flag -Z mir-opt-level requires an argument || unstable option `mir-opt-level`
//@ reject-flags: -Zfmt-debug => Flag -Z fmt-debug requires an argument || unstable option `fmt-debug`
//@ compile-variant: -O

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
