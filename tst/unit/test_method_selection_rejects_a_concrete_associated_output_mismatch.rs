//@ compile-fail: error

trait Factory {
    type Output;
    fn create(&self) -> Self::Output;
}

struct Source;

impl Factory for Source {
    type Output = u16;

    fn create(&self) -> Self::Output {
        37
    }
}

fn main() {
    let source = Source;
    let wrong: (u8, u8) = source.create();
    assert_eq!(wrong, (3, 7));
}
