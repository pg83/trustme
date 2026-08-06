// Extracted from library/core/src/str/mod.rs:514
#![allow(unused)]
fn main() {
    let mut s = String::from("🗻∈🌏");
    
    unsafe {
        let bytes = s.as_bytes_mut();
    
        bytes[0] = 0xF0;
        bytes[1] = 0x9F;
        bytes[2] = 0x8D;
        bytes[3] = 0x94;
    }
    
    assert_eq!("🍔∈🌏", s);
}
