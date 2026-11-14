fn opaque<T: Copy>(value: T) -> impl Copy {
    value
}

struct Slot<T>(T);

impl<T> Slot<T> {
    fn finish(self) -> u8 {
        7
    }
}

fn source(recur: bool) -> (impl Copy,) {
    if recur {
        assert_eq!(Slot(opaque(source(false))).finish(), 7);
    }
    (1u8,)
}

fn main() {
    let _ = source(true);
}
