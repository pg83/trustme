trait Family {
    type Item<T>;
}

struct Echo;

impl Family for Echo {
    type Item<T> = T;
}

impl Echo {
    fn make(self) -> Self {
        self
    }
}

fn accept<F: Family<Item<u16> = u16>>(_family: F) -> u16 {
    37
}

fn main() {
    assert_eq!(accept(Echo.make()), 37);
}
