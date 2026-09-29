// toml_parser writes `debug_assert_eq!(&raw.as_str()[start..end], underscore)`
// with a local named `raw`. `&raw` starts a raw borrow only when `const` or
// `mut` follows (rustc's `parse_borrow_modifiers`); the matcher of an `expr`
// fragment took any `&raw` for one and rejected the argument, so no arm of
// `assert_eq!` matched.
macro_rules! same {
    ($e:expr) => {
        $e
    };
}

fn main() {
    let raw = String::from("1_2");
    assert_eq!(&raw[1..2], "_");
    debug_assert_eq!(&raw.as_str()[1..], "_2");
    let borrowed = same!(&raw);
    assert_eq!(borrowed.len(), 3);

    let mut x = 5u8;
    let p = same!(&raw const x);
    assert_eq!(unsafe { *p }, 5);
    let q = same!(&raw mut x);
    unsafe { *q = 6 };
    assert_eq!(x, 6);
}
