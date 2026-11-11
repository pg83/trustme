trait Word {
    fn word(self) -> u16;
}

impl Word for (u16,) {
    fn word(self) -> u16 {
        self.0
    }
}

impl Word for (u32,) {
    fn word(self) -> u16 {
        self.0 as u16
    }
}

trait TotalWords {
    fn total_words(self) -> u16;
}

impl<I: Iterator> TotalWords for I
where
    I::Item: Word,
{
    fn total_words(self) -> u16 {
        self.fold(0, |total, item| total + item.word())
    }
}

fn main() {
    let total = [7u16, 11, 17]
        .into_iter()
        .filter_map(|value| Some((value,)))
        .total_words();
    assert_eq!(total, 35);
}
