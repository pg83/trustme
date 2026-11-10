
#[macro_use]
fn adjust(value: u16) -> u16 {
    value + 2
}

macro_rules! declare {
    () => {
        fn generated() -> u16 {
            11
        }
    };
}

#[macro_export]
declare!();

#[macro_export]
fn ordinary() -> u16 {
    3
}

fn main() {
    assert_eq!(adjust(5) + generated() + ordinary(), 21);
}
