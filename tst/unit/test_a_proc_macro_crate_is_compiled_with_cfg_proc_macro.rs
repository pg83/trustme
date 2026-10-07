//@ proc-macro-aux-build: cfg_attr_proc_macro.rs
// rustc's default_configuration sets `cfg(proc_macro)` when `--crate-type
// proc-macro` is asked for, and only then.
use cfg_attr_proc_macro::answer;

fn main() {
    assert_eq!(answer!(), 42);
    assert!(!cfg!(proc_macro));
}
