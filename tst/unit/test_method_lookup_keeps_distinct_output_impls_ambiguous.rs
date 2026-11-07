//@ compile-fail: error:
trait Choose<Input, Output> {
    fn choose(&self, input: Input) -> Output;
}

impl<Input> Choose<Input, u16> for () {
    fn choose(&self, _: Input) -> u16 {
        631
    }
}

impl<Input> Choose<Input, u32> for () {
    fn choose(&self, _: Input) -> u32 {
        641
    }
}

fn main() {
    let _ = ().choose(7u8);
}
