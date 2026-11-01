//@ compile-variant: -O
static LEFT: u16 = 7;
static RIGHT: u16 = 13;
fn triple(value: u16) -> u16 { value * 3 }
fn plus_five(value: u16) -> u16 { value + 5 }
struct Entry { value: &'static u16, call: fn(u16) -> u16 }
const ENTRIES: [Entry; 2] = [Entry { value: &LEFT, call: triple }, Entry { value: &RIGHT, call: plus_five }];
#[inline(never)]
fn read(index: usize) -> u16 {
    let entry = &ENTRIES[index];
    (entry.call)(*entry.value)
}
fn main() {
    assert_eq!(read(0), 21);
    assert_eq!(read(1), 18);
}
