trait ConstOnly {
    const read: u8 = 3;
}

struct Reader;

impl Reader {
    fn read(&self) -> u8 {
        37
    }
}

fn main() {
    assert_eq!(Reader.read(), 37);
}
