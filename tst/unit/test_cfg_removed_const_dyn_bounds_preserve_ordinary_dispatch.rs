#![feature(const_trait_impl)]

const trait Measure<'a> {
    fn measure(&self) -> u16;
}

impl<'a> Measure<'a> for u16 {
    fn measure(&self) -> u16 { *self }
}

#[cfg(any())]
type Skipped = dyn for<'a> [const] Measure<'a>;

fn main() {
    let object: &dyn for<'a> Measure<'a> = &523u16;
    assert_eq!(object.measure(), 523);
}
