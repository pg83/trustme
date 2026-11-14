fn opaque<T: Copy>(value: T) -> impl Copy {
    value
}

struct Slot<T>(T);

impl<T> Slot<T> {
    fn finish(self) -> u8 {
        7
    }
}

fn main() {
    let value = opaque(5u8);
    assert_eq!(Slot(value).finish(), 7);
}
