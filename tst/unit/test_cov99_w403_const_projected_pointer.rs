trait Family {
    type Pointer;
}

impl Family for () {
    type Pointer = &'static u16;
}

const fn width<T: Family<Pointer = &'static u16>>(value: T::Pointer) -> usize {
    *value as usize
}

struct Buffer([u8; width::<()>(&3)]);

fn main() {
    let buffer = Buffer([7; 3]);
    assert_eq!(buffer.0, [7, 7, 7]);
}
