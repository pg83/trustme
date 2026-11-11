//@ compile-flags: -O -Zdump-mir
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};

static DATA: [usize; 2] = [19, 31];

fn main() {
    let coroutine = #[coroutine] || {
        let mut value = std::hint::black_box(23usize);
        yield value;

        let original = value;
        let address: *const usize;
        unsafe {
            core::arch::asm!(
                "mov {value}, {original}",
                "add {value}, {step}",
                value = out(reg) value,
                original = in(reg) original,
                step = const 17,
                options(nomem, nostack),
            );
            core::arch::asm!(
                "lea {address}, [rip + {data}]",
                address = out(reg) address,
                data = sym DATA,
                options(nomem, nostack, preserves_flags),
            );
        }
        let borrowed = unsafe { std::slice::from_raw_parts(address, 2) };
        yield value + borrowed[0];
        value + borrowed[1]
    };

    let mut coroutine = Box::pin(coroutine);
    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Yielded(23));
    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Yielded(59));
    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Complete(71));
}
