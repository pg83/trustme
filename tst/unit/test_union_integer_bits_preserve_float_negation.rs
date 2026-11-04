//@ compile-variant: -O
union Bits { integer: u32, float: f32 }

#[inline(always)]
fn selected() -> f32 {
    let bits = Bits { integer: 0x3f80_0000 };
    unsafe { -bits.float }
}

fn main() { assert_eq!(selected(), -1.0); }
