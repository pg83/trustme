#![allow(deprecated)]

struct Named {
    #[deprecated(note = "use the replacement field")]
    code: u8,
}

struct Tuple(#[deprecated(note = "use the replacement field")] u8);

enum Choice {
    #[deprecated(note = "use the replacement variant")]
    Old(u8),
}

trait Read {
    #[deprecated(note = "use the replacement method")]
    #[must_use]
    fn read(&self) -> u8;
}

impl Read for Named {
    fn read(&self) -> u8 { self.code }
}

impl Tuple {
    #[deprecated(note = "use the replacement method")]
    #[must_use]
    fn read(&self) -> u8 { self.0 }
}

fn main() {
    let old = Choice::Old(9);
    let Choice::Old(value) = old;
    assert_eq!(Named { code: 12 }.read() + Tuple(15).read() + value, 36);
}
