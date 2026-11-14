//@ compile-flags: -Z dump-mir -Z dump-hir

trait Family {
    type Value;
}

impl Family for () {
    type Value = u8;
}

struct ByteSource;

impl ByteSource {
    const BYTE: u8 = 37;
}

#[inline(never)]
fn projected<T: Family<Value = u8>>() -> T::Value {
    ByteSource::BYTE
}

fn main() {
    assert_eq!(projected::<()>(), 37);
}
