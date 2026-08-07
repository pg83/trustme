use std::mem;

#[derive(Clone, Copy)]
struct FourBytes {
    word: u16,
    low: u8,
    high: u8,
}

#[derive(Clone, Copy)]
union Value {
    bytes: [u8; 3],
    fields: FourBytes,
}

const VALUE: Value = Value { bytes: [1, 2, 3] };
const SIZE: usize = mem::size_of_val(&VALUE);
const ALIGN: usize = mem::align_of_val(&VALUE);

#[inline(never)]
fn load() -> Value {
    VALUE
}

fn main() {
    assert_eq!(SIZE, mem::size_of::<Value>());
    assert_eq!(ALIGN, mem::align_of::<Value>());
    assert_eq!(unsafe { load().bytes }, [1, 2, 3]);
}
