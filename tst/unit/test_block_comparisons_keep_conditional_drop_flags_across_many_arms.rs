//@ compile-variant: -O
use std::cell::Cell;
struct Guard<'a>(&'a Cell<usize>);
impl Drop for Guard<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}
#[inline(never)]
fn boundary(visits: &Cell<usize>) { visits.set(visits.get() + 1); }
#[inline(never)]
fn finish(value: u32) -> u32 { std::hint::black_box(value).wrapping_mul(3).wrapping_add(1) }

macro_rules! dispatch {
    ($key:expr, $early:expr, $drops:expr, $visits:expr, $value:expr; $( $case:literal => ($($guard:ident),*) ),* $(,)?) => {{
        let output;
        match $key {
            $( $case => {
                $(let $guard = Guard($drops);)*
                $(if $early { drop($guard); })*
                boundary($visits);
                output = finish($value);
            },)*
            _ => unreachable!(),
        }
        output
    }};
}
#[inline(never)]
fn choose(key: u8, early: bool, drops: &Cell<usize>, visits: &Cell<usize>, value: u32) -> u32 {
    dispatch!(key, early, drops, visits, value;
        0 => (),
        1 => (_g0),
        2 => (_g0, _g1),
        3 => (_g0, _g1, _g2),
        4 => (_g0, _g1, _g2, _g3),
        5 => (_g0, _g1, _g2, _g3, _g4),
        6 => (_g0, _g1, _g2, _g3, _g4, _g5),
        7 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6),
        8 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7),
        9 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8),
        10 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8, _g9),
        11 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8, _g9, _g10),
        12 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8, _g9, _g10, _g11),
        13 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8, _g9, _g10, _g11, _g12),
        14 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8, _g9, _g10, _g11, _g12, _g13),
        15 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8, _g9, _g10, _g11, _g12, _g13, _g14),
    )
}
fn main() {
    let drops = Cell::new(0);
    let visits = Cell::new(0);
    for early in [false, true] {
        for key in 0..16u8 {
            drops.set(0);
            let value = key as u32 + 17;
            assert_eq!(choose(std::hint::black_box(key), std::hint::black_box(early), &drops, &visits, value), value * 3 + 1);
            assert_eq!(drops.get(), key as usize);
        }
    }
    assert_eq!(visits.get(), 32);
}
