// Extracted from library/core/src/intrinsics/mod.rs:639
#![allow(unused)]
#![allow(unnecessary_transmutes)]
fn main() {
    let raw_bytes = [0x78, 0x56, 0x34, 0x12];
    
    let num = unsafe {
        std::mem::transmute::<[u8; 4], u32>(raw_bytes)
    };
    
    // use `u32::from_ne_bytes` instead
    let num = u32::from_ne_bytes(raw_bytes);
    // or use `u32::from_le_bytes` or `u32::from_be_bytes` to specify the endianness
    let num = u32::from_le_bytes(raw_bytes);
    assert_eq!(num, 0x12345678);
    let num = u32::from_be_bytes(raw_bytes);
    assert_eq!(num, 0x78563412);
}
