//@ proc-macro-aux-build: associated_constant_identity.rs

use associated_constant_identity::preserve;

#[preserve]
trait Alphabet {
    const BASE: u16 = 367;
}

struct Defaulted;
impl Alphabet for Defaulted {}

struct Changed;
#[preserve]
impl Alphabet for Changed {
    const BASE: u16 = 373;
}

fn main() {
    assert_eq!(<Defaulted as Alphabet>::BASE, 367);
    assert_eq!(<Changed as Alphabet>::BASE, 373);
}
