//@ crate-type: lib
//@ compile-flags: -F dead_code -A dead_code --emit=metadata -Cpanic=abort

pub fn lint_forbid_wins() -> u32 {
    268
}
