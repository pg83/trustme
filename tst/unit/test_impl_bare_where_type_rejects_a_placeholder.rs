//@ compile-fail: error

trait Value {
    fn value() -> u8;
}

struct Entry;

impl Value for Entry
where
    [_; 3]:,
{
    fn value() -> u8 {
        7
    }
}

fn main() {
    assert_eq!(<Entry as Value>::value(), 7);
}
