fn apply(function: fn(u32) -> u32, input: u32) -> u32 {
    function(input)
}

fn increment(value: u32) -> u32 {
    value + 1
}

fn main() {
    assert_eq!(apply(increment, 41), 42);
}
