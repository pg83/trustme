//@ edition: 2021

fn answer() -> u32 {
    1459
}

const DIRECT: fn() -> u32 = answer;
static POINTERS: (fn() -> u32,) = (answer,);
const FROM_STATIC: fn() -> u32 = POINTERS.0;
const ADDRESS: &fn() -> u32 = &POINTERS.0;
const VIA_REFERENCE: fn() -> u32 = *ADDRESS;

fn main() {
    assert_eq!(DIRECT(), 1459);
    assert_eq!(FROM_STATIC(), 1459);
    assert_eq!(VIA_REFERENCE(), 1459);
}
