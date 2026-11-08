//@ compile-flags: -O

static MARKER: u8 = 41;

#[inline(never)]
fn addresses(use_constant: bool) -> (usize, usize) {
    let selected;
    let continuation;
    if use_constant {
        unsafe {
            core::arch::asm!(
                "lea {selected}, [rip + {operand}]",
                "2:",
                "lea {continuation}, [rip + 2b]",
                selected = lateout(reg) selected,
                continuation = lateout(reg) continuation,
                operand = const 0,
                options(nomem, nostack, preserves_flags),
            );
        }
    } else {
        unsafe {
            core::arch::asm!(
                "lea {selected}, [rip + {operand}]",
                "2:",
                "lea {continuation}, [rip + 2b]",
                selected = lateout(reg) selected,
                continuation = lateout(reg) continuation,
                operand = sym MARKER,
                options(nomem, nostack, preserves_flags),
            );
        }
    }
    (selected, continuation)
}

fn main() {
    let (selected, continuation) = addresses(true);
    assert_eq!(selected, continuation);
    let (selected, _) = addresses(false);
    assert_eq!(selected, &MARKER as *const u8 as usize);
}
