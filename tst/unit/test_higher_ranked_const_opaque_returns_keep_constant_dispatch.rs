#![feature(const_trait_impl)]

const trait Measure<'a> {
    fn measure(&self) -> u16;
}

impl<'a> const Measure<'a> for u16 {
    fn measure(&self) -> u16 { *self }
}

const fn make() -> impl for<'a> [const] Measure<'a> + Copy {
    503u16
}

const RESULT: u16 = make().measure();

fn main() {
    assert_eq!(RESULT, 503);
}
