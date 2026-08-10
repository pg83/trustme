//@ compile-fail: type annotations needed

trait Pick {
    fn value() -> u8;
}

impl Pick for u8 {
    fn value() -> u8 {
        8
    }
}

impl Pick for u16 {
    fn value() -> u8 {
        16
    }
}

fn main() {
    let _ = <_ as Pick>::value();
}
