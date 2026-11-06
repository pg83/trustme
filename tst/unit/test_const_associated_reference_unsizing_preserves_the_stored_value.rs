#![feature(unsize)]

use std::marker::Unsize;

trait Measure {
    fn measure(&self) -> u8;
}
trait Detailed: Measure {}

struct Number(u8);
impl Measure for Number {
    fn measure(&self) -> u8 { self.0 }
}
impl Detailed for Number {}

static NUMBER: Number = Number(61);

trait Stored {
    type Value: ?Sized + 'static;
    const REFERENCE: &'static Self::Value;
}
struct Seed;
impl Stored for Seed {
    type Value = dyn Detailed;
    const REFERENCE: &'static dyn Detailed = &NUMBER;
}

const fn select<S: Stored>() -> &'static dyn Measure
where
    S::Value: Unsize<dyn Measure>,
{
    S::REFERENCE
}

const SELECTED: &dyn Measure = select::<Seed>();

fn main() {
    assert_eq!(SELECTED.measure(), 61);
    assert_eq!(SELECTED as *const dyn Measure as *const (), &NUMBER as *const Number as *const ());
}
