// cmov hands `asm!` its instruction through a `$instruction:expr` fragment.
// rustc parses each argument after the first template as a further template
// expression until the first operand, `options` or `clobber_abi`
// (`parse_asm_args`): a string literal in an `expr` fragment, or a
// `concat!`, is a template like a literal.
use std::arch::asm;

macro_rules! add_with {
    ($instruction:expr, $dst:expr, $src:expr) => {
        unsafe {
            asm!(
                "xor {0:e}, {0:e}",
                $instruction,
                inout(reg) $dst,
                in(reg) $src,
                options(pure, nomem, nostack),
            );
        }
    };
}

fn main() {
    let mut dst: u64 = 7;
    let src: u64 = 42;
    add_with!("add {0}, {1}", dst, src);
    assert_eq!(dst, 42);

    let mut other: u64 = 1;
    unsafe {
        asm!("mov {0}, 5", concat!("add {0}, ", "3"), inout(reg) other);
    }
    assert_eq!(other, 8);
}
