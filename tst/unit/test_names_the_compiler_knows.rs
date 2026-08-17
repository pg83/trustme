// A batch of names that have to be known: `repr(transparent)` on an enum, the
// `raw` asm option, and the `expr_2021` macro fragment.
#![crate_type = "bin"]
#![feature(asm_experimental_arch)]

macro_rules! twice {
    ($e:expr_2021) => {
        $e + $e
    };
}

/// A single-variant enum lays out as that variant, which is what `transparent`
/// asks for.
#[repr(transparent)]
enum Wrapper {
    Only(u64),
}

fn unwrap(w: &Wrapper) -> u64 {
    match w {
        Wrapper::Only(v) => *v,
    }
}

/// An enum whose variants are written with `()` and `{}` but hold nothing is
/// still castable to an integer.
#[repr(u8)]
enum Fieldless {
    First = 10,
    Tuple(),
    Second = 20,
    Struct {},
    Unit,
}

fn main() {
    assert_eq!(twice!(21), 42);
    assert_eq!(unwrap(&Wrapper::Only(7)), 7);
    assert_eq!(core::mem::size_of::<Wrapper>(), core::mem::size_of::<u64>());

    assert_eq!(10, Fieldless::First as u8);
    assert_eq!(11, Fieldless::Tuple() as u8);
    assert_eq!(20, Fieldless::Second as u8);
    assert_eq!(21, Fieldless::Struct {} as u8);
    assert_eq!(22, Fieldless::Unit as u8);

    unsafe {
        core::arch::asm!("nop", options(nomem, nostack, raw));
    }
}
