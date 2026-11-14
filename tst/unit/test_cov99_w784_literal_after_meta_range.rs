//@ compile-fail: expected one of
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! literal_after_meta {
    ($meta:meta $value:literal) => {};
}

literal_after_meta!(key = 0..async 1 2);

fn main() {}
