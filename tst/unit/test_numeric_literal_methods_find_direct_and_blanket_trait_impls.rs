trait Score { fn score(&self) -> u16; }
impl Score for u8 { fn score(&self) -> u16 { *self as u16 } }

trait Double { fn double(&self) -> u16; }
impl<T: Score> Double for T {
    fn double(&self) -> u16 { self.score() * 2 }
}

fn main() {
    assert_eq!(17.score(), 17);
    assert_eq!(23.double(), 46);
}
