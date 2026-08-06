// Extracted from library/core/src/str/mod.rs:1305
#![allow(unused)]
fn main() {
    let text = "Zażółć gęślą jaźń";
    
    let utf8_len = text.len();
    let utf16_len = text.encode_utf16().count();
    
    assert!(utf16_len <= utf8_len);
}
