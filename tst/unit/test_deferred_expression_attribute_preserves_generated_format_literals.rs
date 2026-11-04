//@ proc-macro-aux-build: keep_generated_literals.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

macro_rules! import_keep {
    () => { use keep_generated_literals::keep as pending_keep; };
}

fn main() {
    let text = #[pending_keep] { format!("{:04x} {:.3} {}", 43u32, 1.25f64, line!()) };
    import_keep!();
    assert!(text.starts_with("002b 1.250 "));
    let source_line: u32 = text.split_whitespace().last().unwrap().parse().unwrap();
    assert!(source_line > 0);
}
