//@ aux-build: reexported_std_derives.rs
// tor-config (arti) re-exports `std::{clone::Clone, default::Default,
// fmt::Debug}` from `derive::exports`, and its template writes
// `#[derive($crate::derive::exports::Default, ..)]`. Upstream resolves a
// derive path like any macro path, and the re-export leads to the built-in
// derive. We found built-in derives only by a bare name or a `core::`/`std::`
// path and reported "Missing handlers for ::tor_config::derive::exports::Default".
use reexported_std_derives::with_std_derives;

#[derive(reexported_std_derives::exports::Clone, reexported_std_derives::exports::Debug)]
struct Direct(u32);

with_std_derives! {
    struct Through {
        n: u32,
    }
}

fn main() {
    let direct = Direct(4).clone();
    assert_eq!(format!("{:?}", direct), "Direct(4)");
    let through = Through::default().clone();
    assert_eq!(format!("{:?}", through), "Through { n: 0 }");
}
