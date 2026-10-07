// crossterm's `style::types` does `pub use self::attribute::Attribute`, where
// `attribute` both defines `macro_rules! Attribute` (not exported) and the
// `pub enum Attribute` that macro generates. Upstream reports a re-export only
// when no namespace re-exports successfully ("All namespaces must be
// re-exported with extra visibility for an error to occur", `finalize_import`,
// rustc_resolve imports.rs): the enum is re-exported, and we stopped on the macro.
#![allow(dead_code)]

mod types {
    pub use self::attribute::Attribute;

    mod attribute {
        macro_rules! Attribute {
            ($($name:ident),*) => {
                #[derive(Debug, PartialEq)]
                pub enum Attribute { $($name),* }
            };
        }

        Attribute!(Bold, Italic);
    }
}

pub use types::Attribute;

fn main() {
    assert_eq!(format!("{:?}", Attribute::Italic), "Italic");
    assert!(Attribute::Bold != Attribute::Italic);
}
