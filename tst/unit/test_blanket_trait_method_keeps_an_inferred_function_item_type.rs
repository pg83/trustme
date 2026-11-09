trait Evaluate<Function> {
    fn evaluate(&self, function: Function) -> (u16, usize);
}

impl<Function: Fn(u16) -> u16> Evaluate<Function> for () {
    fn evaluate(&self, function: Function) -> (u16, usize) {
        (function(11), std::mem::size_of::<Function>())
    }
}

fn increment(value: u16) -> u16 {
    value + 1
}

fn main() {
    assert_eq!(().evaluate(increment), (12, 0));
}
