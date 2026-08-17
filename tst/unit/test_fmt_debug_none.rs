//@ compile-flags: -Zfmt-debug=none
// `-Zfmt-debug=none` prints nothing for a `Debug` placeholder: the derived
// impls say nothing, and the placeholder itself produces no output even for a
// hand-written `Debug`. The literal text around it stays.
#![feature(fmt_debug)]
#![allow(dead_code)]

#[derive(Debug)]
struct Derived {
    field: u32,
}

#[derive(Debug)]
enum Choice {
    Only,
}

struct Custom;

impl std::fmt::Debug for Custom {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("custom")
    }
}

fn main() {
    assert!(cfg!(fmt_debug = "none"));
    let custom = Custom;
    let text = format!("a'{:?}'b'{:#?}'c'{custom:?}'d", Derived { field: 1 }, Choice::Only);
    assert_eq!(text, "a''b''c''d");

    // Display is untouched.
    assert_eq!(format!("{}-{:?}-{}", 1, 2, 3), "1--3");
}
