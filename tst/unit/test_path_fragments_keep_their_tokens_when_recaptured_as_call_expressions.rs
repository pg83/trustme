struct Packet(u16);

fn increment(value: u16) -> u16 {
    value + 1
}

macro_rules! expression {
    ($value:expr) => { $value };
}

macro_rules! forward {
    ($target:path, $argument:expr) => { expression!($target($argument)) };
}

fn main() {
    assert_eq!(forward!(Packet, 61).0, 61);
    assert_eq!(forward!(crate::increment, 73), 74);
}
