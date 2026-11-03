//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

mod selected {
    use expression_attribute_controls::increment as grow;
    pub fn run() -> u32 {
        let value = #[grow] 31u32;
        value
    }
}

fn main() {
    macro_rules! selected { () => { 37u32 }; }
    let local = #[expression_attribute_controls::local] 0u32;
    assert_eq!(local, 37);
    assert_eq!(selected::run(), 32);
}
