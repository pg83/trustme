#![feature(rustc_attrs)]
mod builtins {
    #[macro_export]
    #[rustc_builtin_macro]
    macro_rules! __build_diagnostic_array { ($($input:tt)*) => {}; }
}
crate::__build_diagnostic_array!(sample, DIAGNOSTICS);
fn main() {
    let entries: &[(&str, &str)] = &DIAGNOSTICS;
    assert!(entries.is_empty());
}
