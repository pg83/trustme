//@ compile-flags: -Cpanic=abort

#[inline(never)]
fn empty_vtables<'a>(free: fn(&'a u8), fixed: fn(&'static u8)) -> (usize, usize) {
    let left: &dyn Send = &free;
    let right: &dyn Send = &fixed;
    (
        std::mem::size_of_val(std::hint::black_box(left)),
        std::mem::size_of_val(std::hint::black_box(right)),
    )
}

fn ignore(_: &u8) {}

fn main() {
    let sizes = empty_vtables(ignore, ignore);
    let expected = std::mem::size_of::<fn(&u8)>();
    assert_eq!(sizes, (expected, expected));
}
