//@ compile-variant: -O
union Bits { boolean: bool, character: char }

#[inline(always)]
fn selected() -> bool {
    let bits = Bits { character: '\0' };
    unsafe { !bits.boolean }
}

fn main() { assert!(selected()); }
