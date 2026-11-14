//@ compile-fail: choose_concrete

trait Item {
    type Value;
}

impl Item for u8 {
    type Value = u16;
}

trait Choose<Output> {
    fn choose_concrete<Input: Item<Value = Output>>(&self, _input: Input) {}
}

fn rejected<R: Choose<u32>>(receiver: R) {
    receiver.choose_concrete(1u8);
}

fn main() {}
