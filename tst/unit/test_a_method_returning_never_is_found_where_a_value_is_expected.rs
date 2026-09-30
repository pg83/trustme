// ron's `Number::into_f64` ends in `Self::__NonExhaustive(never) =>
// never.never()`, a method returning `!` where the match wants an `f64`. The
// method probe weighed candidates against the expected result and dropped the
// only one, whose `!` does not equal `f64`; upstream's probe does not look at
// the expectation, and `!` coerces into any type.
pub struct Absurd(u32);

impl Absurd {
    pub fn absurd(self) -> ! {
        panic!("{}", self.0)
    }
}

pub trait Diverge {
    fn diverge(&self) -> !;
}

impl Diverge for Absurd {
    fn diverge(&self) -> ! {
        panic!("{}", self.0)
    }
}

fn pick(n: Option<Absurd>) -> f64 {
    match n {
        None => 1.0,
        Some(n) => n.absurd(),
    }
}

fn count(b: bool, n: &Absurd) -> u32 {
    if b { 2 } else { n.diverge() }
}

fn main() {
    assert_eq!(pick(None), 1.0);
    assert_eq!(count(true, &Absurd(0)), 2);
}
