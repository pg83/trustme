//@ compile-variant: -O
use std::cell::Cell;
#[inline(always)]
fn require_first(value: u32) { if value == 0 { panic!("zero is not a guard value"); } }
#[inline(always)]
fn require_second(value: u32) { if value == 1 { panic!("one is not a guard value"); } }
struct Guard<'a> { value: u32, drops: &'a Cell<u32> }
impl Drop for Guard<'_> {
    #[inline(always)]
    fn drop(&mut self) {
        require_first(self.value);
        require_second(self.value);
        self.drops.set(self.drops.get() + 1);
    }
}
#[inline(never)]
fn boundary(value: u32) -> u32 { std::hint::black_box(value) }
#[inline(never)]
fn scope(flag: bool, value: u32, drops: &Cell<u32>) -> u32 {
    let _first = Guard { value, drops };
    let _second = Guard { value: value + 2, drops };
    if flag { boundary(value + 3) } else { boundary(value + 5) }
}
fn main() {
    let drops = Cell::new(0);
    assert_eq!(scope(std::hint::black_box(true), std::hint::black_box(7), &drops), 10);
    assert_eq!(drops.get(), 2);
    assert_eq!(scope(std::hint::black_box(false), std::hint::black_box(9), &drops), 14);
    assert_eq!(drops.get(), 4);
}
