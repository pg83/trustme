//@ compile-fail: error:
trait Read<Input, Output> {
    fn read(&self, input: Input) -> Output;
}

impl<T: Copy> Read<&[T], T> for () {
    fn read(&self, input: &[T]) -> T {
        input[0]
    }
}

struct Word(u16);

fn main() {
    let values = [Word(619)];
    let _ = ().read(&values);
}
