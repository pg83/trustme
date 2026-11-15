struct Carry<T>(T);

impl<T> Carry<T> {
    fn into_inner(self) -> T {
        self.0
    }
}

fn main() {
    assert_eq!(Carry(99u32).into_inner(), 99u32);
}
