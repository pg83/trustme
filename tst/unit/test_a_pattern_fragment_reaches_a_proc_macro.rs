// tokio's `select!` is macro_rules that hands its `$bind:pat` fragments to
// the `select_priv_declare_output_enum!`/`select_priv_clean_pattern!` proc
// macros. A captured pattern reaches a proc macro as the pattern's tokens
// (in an invisible group upstream); ours had no way to write one.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo;

macro_rules! matches_pattern {
    ($value:expr, $p:pat) => {
        echo!(match $value {
            $p => true,
            _ => false,
        })
    };
}

fn main() {
    assert!(matches_pattern!(Some(3), Some(1..=5)));
    assert!(!matches_pattern!(Some(9), Some(1..=5)));
    assert!(matches_pattern!((1, 'x'), (_, 'x' | 'y')));
}
