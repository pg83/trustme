// A literal in a pattern is the literal it was written as. Upstream prints `PatKind::Expr(e)`
// by printing that expression (`print_pat`, rustc_ast_pretty/src/pprust/state.rs), so a match
// arm reads `0 =>`; we printed the compiler's own dump of the held value instead
// (`Integer(type = _, value = 0)`), which is not a pattern at all. An attribute macro is
// handed the annotated item as tokens, so that dump is what the macro parses and gives
// back - rstest_reuse's `#[apply(..)]` over base64's engine tests choked on it.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item;

const FOUR: u32 = 4;

#[echo_item]
fn classify(value: u32) -> u32 {
    match value % 4 {
        0 => 10,
        2 => 20,
        _ => 30,
    }
}

#[echo_item]
fn spell(text: &str) -> u32 {
    match text {
        "one" => 1,
        "two" => 2,
        _ => 0,
    }
}

#[echo_item]
fn flag(value: bool) -> u32 {
    match value {
        true => 1,
        false => 0,
    }
}

#[echo_item]
fn letter(value: char) -> u32 {
    match value {
        'a' => 1,
        'x'..='z' => 2,
        _ => 0,
    }
}

#[echo_item]
fn named(value: u32) -> u32 {
    match value {
        FOUR => 1,
        _ => 0,
    }
}

#[echo_item]
fn signed(value: i32) -> u32 {
    match value {
        -7 => 1,
        _ => 0,
    }
}

fn main() {
    assert_eq!(classify(4), 10);
    assert_eq!(classify(6), 20);
    assert_eq!(classify(7), 30);
    assert_eq!(spell("two"), 2);
    assert_eq!(spell("three"), 0);
    assert_eq!(flag(true), 1);
    assert_eq!(flag(false), 0);
    assert_eq!(letter('a'), 1);
    assert_eq!(letter('y'), 2);
    assert_eq!(letter('b'), 0);
    assert_eq!(named(4), 1);
    assert_eq!(named(5), 0);
    assert_eq!(signed(-7), 1);
    assert_eq!(signed(7), 0);
}
