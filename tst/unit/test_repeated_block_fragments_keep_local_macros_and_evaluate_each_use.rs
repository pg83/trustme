//@ compile-variant: -O
macro_rules! pair { ($body:expr) => {{ let a = $body; let b = $body; (a, b) }}; }
fn main() {
    let count = std::cell::Cell::new(0u8);
    let (a, b) = pair!({
        const OFFSET: u8 = 3;
        macro_rules! plus { ($value:expr) => { $value + OFFSET }; }
        let value = count.get();
        count.set(value + 1);
        plus!(value)
    });
    assert_eq!((a, b, count.get()), (3, 4, 2));
}
