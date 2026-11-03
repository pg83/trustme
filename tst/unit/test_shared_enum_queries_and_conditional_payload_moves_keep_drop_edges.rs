//@ compile-variant: -O
use std::cell::Cell;
#[allow(dead_code)]
enum Side { Left(u8), Right(u16) }
#[inline(never)]
fn pick(which: u8, left: &Side, right: &Side) -> u32 {
    match which {
        0 => match left { Side::Left(_) => 11, Side::Right(_) => 19 },
        1 => match right { Side::Left(_) => 11, Side::Right(_) => 19 },
        _ => match left { Side::Left(_) => 11, Side::Right(_) => 19 },
    }
}
struct Token<'a>(&'a Cell<usize>);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
enum Owned<'a> { Left(Token<'a>), Right(Token<'a>) }
#[inline(never)]
fn conditional_drop(variant: bool, take: bool, drops: &Cell<usize>) {
    let value = if variant { Owned::Left(Token(drops)) } else { Owned::Right(Token(drops)) };
    if take {
        match value { Owned::Left(token) | Owned::Right(token) => std::mem::forget(token) }
    }
}
fn main() {
    let left = Side::Left(3);
    let right = Side::Right(5);
    for which in 0..3u8 {
        assert_eq!(pick(std::hint::black_box(which), &left, &right), if which == 1 { 19 } else { 11 });
        assert_eq!(pick(std::hint::black_box(which), &right, &left), if which == 1 { 11 } else { 19 });
    }
    for variant in [false, true] {
        for take in [false, true] {
            let drops = Cell::new(0);
            conditional_drop(std::hint::black_box(variant), std::hint::black_box(take), &drops);
            assert_eq!(drops.get(), if take { 0 } else { 1 });
        }
    }
}
