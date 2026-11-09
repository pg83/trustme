trait Apply<Argument> {
    fn apply(&self, argument: Argument) -> u16;
}

impl Apply<fn(u16) -> u16> for () {
    fn apply(&self, function: fn(u16) -> u16) -> u16 {
        function(7)
    }
}

fn increment(value: u16) -> u16 {
    value + 1
}

fn main() {
    assert_eq!(().apply(increment), 8);
}
