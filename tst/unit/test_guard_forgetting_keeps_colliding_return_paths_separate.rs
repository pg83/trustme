//@ compile-variant: -O
use std::cell::Cell;
struct Guard<'a>(&'a Cell<usize>);
impl Drop for Guard<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
#[inline(never)]
fn boundary(visits: &Cell<usize>) { visits.set(visits.get() + 1); }
#[inline(never)]
fn finish(value: u32) -> u32 { std::hint::black_box(value).wrapping_mul(3).wrapping_add(1) }
#[inline(never)]
fn discard<T>(value: T, recorded: &Cell<usize>) {
    recorded.set(std::mem::size_of::<T>() / std::mem::size_of::<Guard<'_>>());
    std::mem::forget(value);
}
macro_rules! dispatch {
    ($key:expr, $drops:expr, $visits:expr, $value:expr, $recorded:expr; $( $case:literal => ($($guard:ident),*) ),* $(,)?) => {{
        let output;
        match $key {
            $( $case => {
                $(let $guard = Guard($drops);)*
                boundary($visits);
                output = finish($value);
                discard(($($guard,)*), $recorded);
            },)*
            _ => unreachable!(),
        }
        output
    }};
}
#[inline(never)]
fn choose(key: u8, drops: &Cell<usize>, visits: &Cell<usize>, value: u32, recorded: &Cell<usize>) -> u32 {
    dispatch!(key, drops, visits, value, recorded;
        0 => (_g0),
        1 => (_g0, _g1),
        2 => (_g0, _g1, _g2),
        3 => (_g0, _g1, _g2, _g3),
        4 => (_g0, _g1, _g2, _g3, _g4, _g5, _g6, _g7, _g8, _g9, _g10, _g11, _g12, _g13, _g14, _g15, _g16, _g17, _g18, _g19, _g20, _g21, _g22, _g23, _g24, _g25, _g26, _g27, _g28, _g29, _g30, _g31, _g32, _g33, _g34, _g35, _g36, _g37, _g38, _g39, _g40, _g41, _g42, _g43, _g44, _g45, _g46, _g47, _g48, _g49, _g50, _g51, _g52, _g53, _g54, _g55, _g56, _g57, _g58, _g59, _g60, _g61, _g62, _g63, _g64, _g65, _g66, _g67, _g68, _g69, _g70, _g71, _g72, _g73, _g74, _g75, _g76, _g77, _g78, _g79, _g80, _g81, _g82, _g83, _g84, _g85, _g86, _g87, _g88, _g89, _g90, _g91, _g92, _g93, _g94, _g95, _g96, _g97, _g98, _g99, _g100, _g101, _g102, _g103, _g104, _g105, _g106, _g107, _g108, _g109, _g110, _g111, _g112, _g113, _g114, _g115, _g116, _g117, _g118, _g119, _g120, _g121, _g122, _g123, _g124, _g125, _g126, _g127, _g128, _g129, _g130, _g131, _g132, _g133, _g134, _g135, _g136, _g137, _g138, _g139, _g140, _g141, _g142, _g143, _g144, _g145, _g146, _g147, _g148, _g149, _g150, _g151, _g152, _g153, _g154, _g155, _g156, _g157, _g158, _g159, _g160, _g161, _g162, _g163, _g164, _g165, _g166, _g167, _g168, _g169, _g170, _g171, _g172, _g173, _g174, _g175, _g176, _g177, _g178, _g179, _g180, _g181, _g182, _g183, _g184, _g185, _g186, _g187, _g188, _g189, _g190, _g191, _g192, _g193, _g194),
    )
}
fn main() {
    let drops = Cell::new(0);
    let visits = Cell::new(0);
    let recorded = Cell::new(0);
    for (key, expected) in [1usize, 2, 3, 4, 195].iter().copied().enumerate() {
        recorded.set(0);
        let value = key as u32 + 23;
        assert_eq!(choose(std::hint::black_box(key as u8), &drops, &visits, value, &recorded), value * 3 + 1);
        assert_eq!(recorded.get(), expected);
        assert_eq!(drops.get(), 0);
    }
    assert_eq!(visits.get(), 5);
}
