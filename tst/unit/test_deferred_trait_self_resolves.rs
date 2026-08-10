//@ check-pass

trait Make {
    fn make() -> Self;
}

impl Make for u8 {
    fn make() -> Self {
        8
    }
}

fn main() {
    let value: u8 = <_ as Make>::make();
    assert_eq!(value, 8);
}
