//@ test-harness
#![feature(try_blocks, coroutines, coroutine_trait, stmt_expr_attributes)]

#[test]
#[should_panic(expected = "'exit: loop")]
fn labelled_break_value() {
    assert!('exit: loop {
        break 'exit { false };
    });
}

#[test]
#[should_panic(expected = "break 'scope false")]
fn labelled_block_value() {
    assert!('scope: {
        for value in 0..3 {
            if value == 2 { break 'scope false; }
        }
        true
    });
}

#[test]
#[should_panic(expected = "continue 'outer")]
fn while_and_continue() {
    assert!({
        let mut value = 3;
        'outer: while value > 0 {
            value -= 1;
            if value == 1 { continue 'outer; }
        }
        value != 0
    });
}

#[test]
#[should_panic(expected = "async move")]
fn asynchronous_block() {
    assert!({
        let _future = async move {
            let value = async { 2u8 }.await;
            value + 1
        };
        false
    });
}

#[test]
#[should_panic(expected = "yield 1")]
fn coroutine_expression() {
    assert!({
        let _coroutine = #[coroutine] || { yield 1; return 2; };
        false
    });
}

#[test]
#[should_panic(expected = "try {")]
fn try_block() {
    assert!({
        let result: Result<bool, u8> = try { let _ = Ok::<_, u8>(1)?; false };
        result.unwrap()
    });
}

#[test]
#[should_panic(expected = "&raw mut value")]
fn raw_borrows_and_assignments() {
    assert!({
        let mut value = 7u32;
        let _constant = &raw const value;
        let _mutable = &raw mut value;
        value += 2;
        value -= 1;
        value *= 3;
        value /= 2;
        value %= 5;
        value &= 7;
        value |= 8;
        value ^= 2;
        value <<= 1;
        value >>= 1;
        value == 0
    });
}

#[test]
#[should_panic(expected = "const {")]
fn inline_const() {
    assert!(const { false });
}
