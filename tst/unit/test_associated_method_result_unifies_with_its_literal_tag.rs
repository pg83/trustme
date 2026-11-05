trait Emit<Tag> {
    type Output;
    fn emit(self, tag: &Tag) -> Self::Output;
}

struct Value<T>(T);

impl<T> Emit<u8> for Value<T> {
    type Output = T;

    fn emit(self, _: &u8) -> T {
        self.0
    }
}

fn pair<T>(first: T, second: T) -> (T, T) {
    (first, second)
}

fn main() {
    let tag = 1;
    let values = pair(tag, Value(2).emit(&tag));
    assert_eq!(values, (1u8, 2u8));
}
