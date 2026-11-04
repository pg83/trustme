//@ aux-build: builtin_derive_imports.rs
//@ compile-variant: -O

#[macro_use(DisplayRecord, DuplicateRecord)]
extern crate builtin_derive_imports;

#[derive(DisplayRecord, DuplicateRecord)]
struct Ledger { amount: u16, label: &'static str }

fn main() {
    let original = Ledger { amount: 73, label: "birch" };
    let copied = original.clone();
    assert_eq!(copied.amount, 73);
    assert_eq!(copied.label, "birch");
    assert_eq!(format!("{:?}", original), "Ledger { amount: 73, label: \"birch\" }");
}
