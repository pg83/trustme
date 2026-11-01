#![feature(deref_patterns)]
#![allow(incomplete_features)]

#[inline(never)]
fn text(value: &&String, enabled: bool) -> u8 {
    match value {
        deref!("ready") if enabled => 7,
        deref!("ready") | deref!("wait") => 11,
        _ => 13,
    }
}

#[inline(never)]
fn boxed(value: &&Box<(u8, u8)>) -> u8 {
    match value {
        deref!((3, n)) | deref!((5, n)) => *n,
        _ => 0,
    }
}

fn main() {
    assert_eq!(text(&&String::from("ready"), true), 7);
    assert_eq!(text(&&String::from("ready"), false), 11);
    assert_eq!(text(&&String::from("wait"), true), 11);
    assert_eq!(text(&&String::from("other"), false), 13);
    assert_eq!(boxed(&&Box::new((3, 19))), 19);
    assert_eq!(boxed(&&Box::new((5, 23))), 23);
    assert_eq!(boxed(&&Box::new((7, 29))), 0);
}
