//@ compile-fail: No applicable methods

trait Source {
    type Item;
}

struct Input;
impl Source for Input {
    type Item = u16;
}

trait Select<Result, Tag> {
    fn select<S>(&self, source: S) -> Result
    where
        S: Source<Item = Result>;
}

struct Picker;
impl Select<u8, bool> for Picker {
    fn select<S>(&self, _: S) -> u8
    where
        S: Source<Item = u8>,
    {
        71
    }
}

fn main() {
    let _: u8 = Picker.select(Input);
}
