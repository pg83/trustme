// time's `format_float` writes `{value:0>width$.digits_after_decimal$}` where
// `digits_after_decimal` comes out of `.extend()`, generic in its result: only
// the precision fixes it. rustc lowers a `$` count to `Count::Param(i)` with
// `Argument::from_usize(&arg)` in the argument list, so the count argument is
// a `usize`, and one past `u16::MAX` panics.
use std::num::NonZeroU8;

trait Ext<T> {
    fn ext(self) -> T;
}

impl Ext<usize> for u8 {
    fn ext(self) -> usize {
        self as usize
    }
}

impl Ext<u16> for u8 {
    fn ext(self) -> u16 {
        self as u16
    }
}

fn render(value: f64, before: u8, after: NonZeroU8) -> String {
    let after = after.get().ext();
    let width = Ext::<usize>::ext(before) + 1 + after;
    format!("{value:0>width$.after$}")
}

fn main() {
    assert_eq!(render(3.5, 2, NonZeroU8::new(2).unwrap()), "03.50");
    assert_eq!(format!("[{:>1$}|{:<1$}]", 7, 3), "[  7|3  ]");
    let wide = std::panic::catch_unwind(|| format!("{:1$}", 1, 70000usize));
    assert!(wide.is_err());
}
