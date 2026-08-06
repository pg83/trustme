// Extracted from library/core/src/str/mod.rs:618
#![allow(unused)]
fn main() {
    let mut v = String::from("hello");
    // correct length
    assert!(v.get_mut(0..5).is_some());
    // out of bounds
    assert!(v.get_mut(..42).is_none());
    assert_eq!(Some("he"), v.get_mut(0..2).map(|v| &*v));
    
    assert_eq!("hello", v);
    {
        let s = v.get_mut(0..2);
        let s = s.map(|s| {
            s.make_ascii_uppercase();
            &*s
        });
        assert_eq!(Some("HE"), s);
    }
    assert_eq!("HEllo", v);
}
