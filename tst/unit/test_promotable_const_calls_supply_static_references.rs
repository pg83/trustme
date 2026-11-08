#![feature(rustc_attrs, staged_api)]
#![stable(feature = "own_promoted_calls", since = "1.0.0")]

#[stable(feature = "own_promoted_calls", since = "1.0.0")]
#[rustc_const_stable(feature = "own_promoted_calls", since = "1.0.0")]
#[rustc_promotable]
const fn base(value: u16) -> u16 {
    value + 4
}

struct Scale;
impl Scale {
    #[stable(feature = "own_promoted_calls", since = "1.0.0")]
    #[rustc_const_stable(feature = "own_promoted_calls", since = "1.0.0")]
    #[rustc_promotable]
    const fn scale(&self, value: u16) -> u16 {
        value * 3
    }
}

fn values() -> (&'static u16, &'static u16) {
    (&base(7), &Scale.scale(5))
}

fn main() {
    let (left, right) = values();
    assert_eq!((*left, *right), (11, 15));
}
