// Extracted from library/core/src/char/methods.rs:136
#![allow(unused)]
fn main() {
    // 𝄞mus<invalid>ic<invalid>
    let v = [
        0xD834, 0xDD1E, 0x006d, 0x0075, 0x0073, 0xDD1E, 0x0069, 0x0063, 0xD834,
    ];
    
    assert_eq!(
        char::decode_utf16(v)
           .map(|r| r.unwrap_or(char::REPLACEMENT_CHARACTER))
           .collect::<String>(),
        "𝄞mus�ic�"
    );
}
