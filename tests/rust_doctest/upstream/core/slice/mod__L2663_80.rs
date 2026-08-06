// Extracted from library/core/src/slice/mod.rs:2663
#![allow(unused)]
fn main() {
    let v = &[10, 40, 30];
    assert_eq!(v.strip_prefix(&[10]), Some(&[40, 30][..]));
    assert_eq!(v.strip_prefix(&[10, 40]), Some(&[30][..]));
    assert_eq!(v.strip_prefix(&[10, 40, 30]), Some(&[][..]));
    assert_eq!(v.strip_prefix(&[50]), None);
    assert_eq!(v.strip_prefix(&[10, 50]), None);
    
    let prefix : &str = "he";
    assert_eq!(b"hello".strip_prefix(prefix.as_bytes()),
               Some(b"llo".as_ref()));
}
