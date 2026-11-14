//@ compile-flags: -Z dump-mir -Z dump-hir

trait Family {
    type Value<'a>;
}

impl Family for () {
    type Value<'a> = u8;
}

struct ByteSource;

impl ByteSource {
    const BYTE: u8 = 37;
}

#[inline(never)]
fn projected<'a, T: Family>() -> T::Value<'a>
where
    for<'b> T: Family<Value<'b> = u8>,
{
    ByteSource::BYTE
}

fn main() {
    assert_eq!(projected::<()>(), 37);
}
