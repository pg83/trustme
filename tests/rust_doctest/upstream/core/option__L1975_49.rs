// Extracted from library/core/src/option.rs:1975
#![allow(unused)]
fn main() {
    let x = Some((1, "hi"));
    let y = None::<(u8, u32)>;
    
    assert_eq!(x.unzip(), (Some(1), Some("hi")));
    assert_eq!(y.unzip(), (None, None));
}
