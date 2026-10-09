// libcore 1.93's `array::try_from_fn` bounds `R: [const] Try<Residual: [const]
// Residual<..>, Output: [const] Destruct>`. rustc parses an associated item
// constraint's bounds with `parse_generic_bounds`, constness modifiers
// included, and the projection's bound keeps its constness. We parsed only an
// optional `for<..>` and a path there, and dropped the constness when the
// constraint became a bound on the projection.
#![feature(const_trait_impl, const_destruct)]

use std::marker::Destruct;

const trait Answer {
    fn answer(&self) -> u32;
}

const trait Holder {
    type Item;
    fn item(&self) -> Self::Item;
}

struct Forty;
struct Box42;

impl const Answer for Forty {
    fn answer(&self) -> u32 {
        42
    }
}

impl const Holder for Box42 {
    type Item = Forty;
    fn item(&self) -> Forty {
        Forty
    }
}

const fn answer_of<T: [const] Holder<Item: [const] Answer + [const] Destruct>>(holder: &T) -> u32 {
    holder.item().answer()
}

const AT_COMPILE_TIME: u32 = answer_of(&Box42);

fn main() {
    assert_eq!(AT_COMPILE_TIME, 42);
    assert_eq!(answer_of(&Box42), 42);
}
