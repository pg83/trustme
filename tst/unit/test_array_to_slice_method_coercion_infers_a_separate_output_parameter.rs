trait Read<Input, Output> {
    fn read(&self, input: Input) -> Output;
}

impl<T: Copy> Read<&[T], T> for () {
    fn read(&self, input: &[T]) -> T {
        input[0]
    }
}

fn main() {
    let values = [613u16, 617u16];
    let output = ().read(&values);
    assert_eq!(output, 613u16);
}
