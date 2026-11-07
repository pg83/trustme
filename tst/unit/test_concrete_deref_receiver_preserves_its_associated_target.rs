use std::ops::Deref;

struct Word(u16);

impl Deref for Word {
    type Target = u16;

    fn deref(&self) -> &u16 {
        &self.0
    }
}

trait DoubleValue {
    fn double_value(&self) -> u16;
}

impl DoubleValue for u16 {
    fn double_value(&self) -> u16 {
        *self * 2
    }
}

fn main() {
    let word = Word(173);
    assert_eq!(word.double_value(), 346);
}
