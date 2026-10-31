//@ proc-macro-aux-build: nested_cfg_token_checks.rs
use nested_cfg_token_checks::{raw_shape, OnlyLive};

#[raw_shape]
#[derive(OnlyLive)]
struct Named {
    #[cfg_attr(all(), cfg(any()))]
    removed: Missing,
    kept: u16,
}
#[derive(OnlyLive)]
struct Tuple(#[cfg_attr(all(), cfg(any()))] Missing, u32);
#[derive(OnlyLive)]
enum Choice {
    #[cfg_attr(all(), cfg(any()))]
    removed(Missing),
    Kept(#[cfg_attr(all(), cfg(any()))] Missing, u64),
}
#[derive(OnlyLive)]
union Bits {
    #[cfg_attr(all(), cfg(any()))]
    removed: Missing,
    kept: u32,
}
fn main() {
    assert_eq!(Named { kept: 71 }.kept, 71);
    let Tuple(value) = Tuple(73);
    assert_eq!(value, 73);
    let Choice::Kept(value) = Choice::Kept(79);
    assert_eq!(value, 79);
    assert_eq!(unsafe { Bits { kept: 83 }.kept }, 83);
}
