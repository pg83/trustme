struct Record(u16);

trait Code {
    fn code() -> u16;
}

impl Code for Record {
    fn code() -> u16 {
        1053
    }
}

impl Record {
    fn value() -> u16 {
        1051
    }
}

fn main() {
    let Record(value) = Record(1049);
    let (left, [middle, right]) = (value, [2u16, 3u16]);
    let &borrowed = &5u16;
    assert_eq!(left + middle + right + borrowed, 1059);
    assert_eq!(<Record as Code>::code(), 1053);
    assert_eq!(<Record>::value(), 1051);
}
