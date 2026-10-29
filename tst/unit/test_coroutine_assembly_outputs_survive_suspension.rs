//@ compile-flags: -Copt-level=2
#![feature(coroutines, coroutine_trait, stmt_expr_attributes, asm_goto_with_outputs)]
use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;
use std::arch::asm;
static OFFSET: u64 = 7;
fn main() {
    let mut output = #[coroutine] || {
        let value: u64;
        unsafe { asm!("mov {value}, 31", value = lateout(reg) value, options(nomem, nostack)); }
        yield 0u64;
        value
    };
    assert_eq!(Pin::new(&mut output).resume(()), CoroutineState::Yielded(0));
    assert_eq!(Pin::new(&mut output).resume(()), CoroutineState::Complete(31));
    let mut overwritten = #[coroutine] || {
        let carried = std::hint::black_box(91u64);
        let mut value = std::hint::black_box(11u64);
        unsafe {
            asm!("mov {value}, qword ptr [rip + {origin}]", "add {value}, {bias}",
                value = out(reg) value, origin = sym OFFSET, bias = const 5,
                options(readonly, nostack));
        }
        yield value;
        value + carried
    };
    assert_eq!(Pin::new(&mut overwritten).resume(()), CoroutineState::Yielded(12));
    assert_eq!(Pin::new(&mut overwritten).resume(()), CoroutineState::Complete(103));
    for selector in [0u64, 1] {
        let mut branched = #[coroutine] move || {
            let mut value = std::hint::black_box(5u64);
            let mut route = 0u64;
            unsafe {
                asm!("add {value}, {step}", "test {selector}, {selector}", "jz {zero}",
                    value = inout(reg) value, selector = in(reg) selector, step = const 3,
                    zero = label { route = 17; }, options(nostack));
            }
            yield value;
            value + route
        };
        assert_eq!(Pin::new(&mut branched).resume(()), CoroutineState::Yielded(8));
        assert_eq!(Pin::new(&mut branched).resume(()), CoroutineState::Complete(if selector == 0 { 25 } else { 8 }));
    }
}
