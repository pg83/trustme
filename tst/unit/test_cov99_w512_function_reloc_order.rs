#![feature(adt_const_params, unsized_const_params)]

static NAMED: [u8; 1] = [17];

#[inline(never)]
fn first<const DATA: &'static [u8; 1]>() -> u8 {
    DATA[0]
}

fn main() {
    assert_eq!(first::<{ &NAMED }>(), 17);
    assert_eq!(first::<{ b"\x2b" }>(), 43);
}
