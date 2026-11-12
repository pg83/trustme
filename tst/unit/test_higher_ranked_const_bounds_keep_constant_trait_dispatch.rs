#![feature(const_trait_impl)]

const trait Measure<'a> {
    fn measure(&self) -> u16;
}

impl<'a> const Measure<'a> for u16 {
    fn measure(&self) -> u16 { *self }
}

const fn compute<T: for<'a> [const] Measure<'a>>(value: &T) -> u16 {
    <T as Measure<'static>>::measure(value)
}

const RESULT: u16 = compute(&487u16);

fn main() {
    assert_eq!(RESULT, 487);
}
