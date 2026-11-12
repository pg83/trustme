#![feature(const_trait_impl)]

const trait Measure<'a> {
    fn measure(&self) -> u16;
}

const trait Child: for<'a> [const] Measure<'a> {}

impl<'a> const Measure<'a> for u16 {
    fn measure(&self) -> u16 { *self + 2 }
}

impl const Child for u16 {}

struct Runtime(u16);

impl<'a> Measure<'a> for Runtime {
    fn measure(&self) -> u16 { self.0 + 4 }
}

impl Child for Runtime {}

const fn compute<T: [const] Child>(value: &T) -> u16 {
    <T as Measure<'static>>::measure(value)
}

const RESULT: u16 = compute(&491u16);

fn main() {
    assert_eq!(RESULT, 493);
    assert_eq!(compute(&Runtime(499)), 503);
}
