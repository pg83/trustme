//@ compile-variant: -O
//@ compile-variant: -O -Cpanic=abort
fn recursive(value: u32) -> u32 {
    loop { let _ = recursive(value); }
}

#[inline(never)]
fn choose(stop: bool, value: u32) -> u32 {
    if stop { recursive(value) } else { value ^ 0x37 }
}

fn main() {
    assert_eq!(choose(std::hint::black_box(false), 29), 29 ^ 0x37);
}
