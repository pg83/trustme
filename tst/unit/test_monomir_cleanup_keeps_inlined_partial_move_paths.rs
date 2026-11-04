//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Cpanic=unwind -Zmir-opt-level=2
use std::cell::Cell;
struct Mark<'a>(u32, &'a Cell<u32>);
impl Drop for Mark<'_> {
    #[inline(always)]
    fn drop(&mut self) { self.1.set(self.1.get() + self.0); }
}
#[inline(always)]
fn choose(flag: bool, count: &Cell<u32>, callback: fn(&Cell<u32>)) {
    let first = Mark(83, count);
    let second = Mark(89, count);
    if flag { drop(first); callback(count); }
    else { drop(second); callback(count); }
}
#[inline(never)]
pub extern "C" fn boundary(flag: bool, count: &Cell<u32>, callback: fn(&Cell<u32>)) {
    choose(flag, count, callback);
}
fn visit(count: &Cell<u32>) { count.set(count.get() + 97); }
fn main() {
    let count = Cell::new(0);
    boundary(std::hint::black_box(true), &count, std::hint::black_box(visit));
    assert_eq!(count.get(), 269);
}
