//@ compile-flags: -C overflow_checks=true

use std::hint::black_box;
use std::panic::{catch_unwind, UnwindSafe};

fn panics(f: impl FnOnce() + UnwindSafe) {
    assert!(catch_unwind(f).is_err());
}

fn main() {
    panics(|| { let _ = black_box(u8::MAX) + black_box(1); });
    panics(|| { let _ = black_box(i8::MIN) - black_box(1); });
    panics(|| { let _ = black_box(u16::MAX) * black_box(2); });
    panics(|| { let _ = black_box(1u8) << black_box(8u32); });
    panics(|| { let _ = black_box(1u8) >> black_box(-1i8); });
    panics(|| { let _ = -black_box(i8::MIN); });

    panics(|| { let mut value = black_box(u8::MAX); value += black_box(1); black_box(value); });
    panics(|| { let mut value = black_box(i8::MIN); value -= black_box(1); black_box(value); });
    panics(|| { let mut value = black_box(u16::MAX); value *= black_box(2); black_box(value); });
    panics(|| { let mut value = black_box(1u8); value <<= black_box(8u32); black_box(value); });

    // Rust checks the two division traps even when ordinary overflow checks
    // are disabled; keeping them here also exercises the shared panic path.
    panics(|| { let _ = black_box(i8::MIN) / black_box(-1); });
    panics(|| { let _ = black_box(i8::MIN) % black_box(-1); });
    panics(|| { let _ = black_box(1u8) / black_box(0); });
}
