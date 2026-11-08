#![feature(rustc_attrs)]

#[rustc_on_unimplemented(message = "an offset provider is required")]
trait Offset {
    #[rustc_inherit_overflow_checks]
    fn apply(&self, value: u16) -> u16 {
        value + 2
    }
}

struct Step;
impl Offset for Step {}

impl Step {
    #[rustc_inherit_overflow_checks]
    fn twice(&self, value: u16) -> u16 {
        value * 2
    }
}

#[rustc_inherit_overflow_checks]
fn increment(value: u16) -> u16 {
    value + 1
}

fn main() {
    let step = Step;
    assert_eq!(increment(step.twice(step.apply(7))), 19);
}
