//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// aws-lc-rs writes `assert_eq!(*$expect_tag_len, $alg.tag_len())` inside
// `paste! { .. }` with `$alg = &AES_128_GCM`. Upstream hands paste the
// fragment as an invisible group, and paste gives back the function body it
// did not change as the very group it received. A tree the macro never
// iterated to keeps the compiler's own invisible delimiter, so `$alg` stays one
// operand and `$alg.tag_len()` is a `usize`; a group the macro was handed by
// iterating comes back as one it made, which the parser reads through
// (`InvisibleOrigin::ProcMacro`). We wrote every invisible group back without
// delimiters, and `&AES_128_GCM.tag_len()` was a `&usize`.
extern crate proc_macro_item_passthrough;
use proc_macro_item_passthrough::{echo, reemit};

macro_rules! untouched {
    ($e:expr) => {
        echo!($e * 2)
    };
}

macro_rules! iterated {
    ($e:expr) => {
        reemit!($e * 2)
    };
}

macro_rules! inside_an_untouched_group {
    ($e:expr) => {
        reemit!({ $e * 2 })
    };
}

macro_rules! len_of {
    ($e:expr) => {
        reemit!({ $e.len() })
    };
}

macro_rules! forwarded_type {
    ($t:ty) => {
        forwarded_type_again!($t);
    };
}

macro_rules! forwarded_type_again {
    ($u:ty) => {
        echo! { const FORWARDED: usize = core::mem::size_of::<$u>(); }
    };
}

forwarded_type!([u32; 3]);

fn main() {
    assert_eq!(FORWARDED, 12);
    assert_eq!(untouched!(1 + 2), 6);
    assert_eq!(iterated!(1 + 2), 5);
    assert_eq!(inside_an_untouched_group!(1 + 2), 6);
    let s = String::from("abc");
    let n: usize = len_of!(&s);
    assert_eq!(n, 3);
}
