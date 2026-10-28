#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

fn main() {
    let seed = std::hint::black_box(5u64);
    let mut coroutine = #[coroutine] move || {
        let mut value = seed;
        unsafe {
            std::arch::asm!("add {value}, {step}", value = inout(reg) value,
                step = const 3, options(nomem, nostack));
        }
        yield value;
        unsafe {
            std::arch::asm!("jmp {target}", target = label { value += 11; });
        }
        yield value;
        let result: u64;
        unsafe {
            std::arch::asm!("mov rax, {value}", value = in(reg) value,
                lateout("rax") result, options(nomem, nostack));
        }
        result
    };
    assert_eq!(Pin::new(&mut coroutine).resume(()), CoroutineState::Yielded(8));
    assert_eq!(Pin::new(&mut coroutine).resume(()), CoroutineState::Yielded(19));
    assert_eq!(Pin::new(&mut coroutine).resume(()), CoroutineState::Complete(19));
}
