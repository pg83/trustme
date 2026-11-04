//@ proc-macro-aux-build: associated_const_path_tokens.rs
//@ compile-variant: -O
#![feature(cfg_version)]
#![cfg_attr(version("1.94"), feature(min_generic_const_args))]
#![cfg_attr(not(version("1.94")), feature(associated_const_equality))]

extern crate associated_const_path_tokens;

trait Holder {
    const COUNT: usize;
}

macro_rules! render_path {
    ($path:path) => { associated_const_path_tokens::render!($path) };
}

fn compact(text: &str) -> String {
    text.chars().filter(|ch| !ch.is_whitespace()).collect()
}

fn main() {
    assert_eq!(compact(render_path!(Holder<COUNT = 13>)), "Holder<COUNT=13>");
    assert_eq!(compact(render_path!(Holder<COUNT = { 3 + 4 }>)), "Holder<COUNT={3+4}>");
}
