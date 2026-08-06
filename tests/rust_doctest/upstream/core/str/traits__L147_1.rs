// Extracted from library/core/src/str/traits.rs:147
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard";
    assert_eq!(&s[0 .. 1], "L");
    
    assert_eq!(&s[1 .. 9], "öwe 老");
    
    // these will panic:
    // byte 2 lies within `ö`:
    // &s[2 ..3];
    
    // byte 8 lies within `老`
    // &s[1 .. 8];
    
    // byte 100 is outside the string
    // &s[3 .. 100];
}
