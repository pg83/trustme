macro_rules! some_else {
    ($value:expr => $alternative:expr) => {
        match $value {
            Some(value) => value,
            None => $alternative,
        }
    };
}

struct Stream;

impl Stream {
    fn consume(&mut self) -> Option<u32> {
        Some(17)
    }
}

fn main() {
    let mut it = Stream;
    for _ in 0..2 {
        let value = some_else!(it.consume() => return);
        assert_eq!(value, 17);
    }
}
