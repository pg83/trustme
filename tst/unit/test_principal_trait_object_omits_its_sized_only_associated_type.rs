trait Stream {
    type Word;
    type ConcreteOnly where Self: Sized;
    fn word(&self) -> Self::Word;
}

impl Stream for bool {
    type Word = u16;
    type ConcreteOnly = [u8; 3];
    fn word(&self) -> u16 { if *self { 59 } else { 61 } }
}

fn main() {
    let first: &dyn Stream<Word = _> = &true;
    let second: &dyn Stream<Word = u16> = &false;
    assert_eq!((first.word(), second.word()), (59, 61));
}
