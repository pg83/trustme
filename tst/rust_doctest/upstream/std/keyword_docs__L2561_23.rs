// Extracted from library/std/src/keyword_docs.rs:2561
#![allow(unused)]
fn main() {
    union IntOrFloat {
        i: u32,
        f: f32,
    }

    let mut u = IntOrFloat { f: 1.0 };
    // Reading the fields of a union is always unsafe
    assert_eq!(unsafe { u.i }, 1065353216);
    // Updating through any of the field will modify all of them
    u.i = 1073741824;
    assert_eq!(unsafe { u.f }, 2.0);
}
