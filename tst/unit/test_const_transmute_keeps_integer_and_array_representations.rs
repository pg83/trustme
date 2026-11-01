const VALUE: u32 = unsafe { core::mem::transmute([0x31u8, 0x42, 0x53, 0x64]) };
const BACK: [u8; 4] = unsafe { core::mem::transmute(VALUE) };
fn main() {
    assert_eq!(VALUE, u32::from_ne_bytes([0x31, 0x42, 0x53, 0x64]));
    assert_eq!(BACK, [0x31, 0x42, 0x53, 0x64]);
}
