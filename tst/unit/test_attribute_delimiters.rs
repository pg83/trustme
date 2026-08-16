// An attribute's arguments may use any delimiter, so `#[attr{...}]` is as valid
// as `#[attr(...)]`. Only parentheses and brackets were accepted.
//
// Same shape as the upstream test attributes/unrestricted-attribute-tokens.rs.
#![feature(rustc_attrs)]

#[rustc_dummy(a b c d)]
#[rustc_dummy[a b c d]]
#[rustc_dummy{a b c d}]
fn tagged() -> u32 {
    1
}

#[rustc_dummy{}]
struct S;

fn main() {
    assert_eq!(tagged(), 1);
    let _ = S;
}
