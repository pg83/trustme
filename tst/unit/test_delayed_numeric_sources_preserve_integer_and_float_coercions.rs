trait Source {
    type Value;
    fn read() -> Self::Value;
}

struct Word;
impl Source for Word {
    type Value = u16;
    fn read() -> u16 { 239 }
}

struct Fraction;
impl Source for Fraction {
    type Value = f64;
    fn read() -> f64 { 0.75 }
}

fn sample<S: Source>(_: S) -> S::Value {
    S::read()
}

fn choose(flag: bool) -> (u16, f64) {
    let mut integer = 0;
    let mut real = 0.0;
    integer = if flag { sample(Word) } else { 251 };
    real = if flag { 1.25 } else { sample(Fraction) };
    (integer, real)
}

fn main() {
    assert_eq!(choose(true), (239, 1.25));
    assert_eq!(choose(false), (251, 0.75));
}
