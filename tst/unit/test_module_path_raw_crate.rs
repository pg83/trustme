// A crate may be named after a reserved word, and `module_path!` names it the
// only way it could be written: as a raw identifier.
//
// Same shape as the upstream test
// attributes/z-crate-attr/respect-existing-attrs.rs.
#![crate_name = "override"]

mod inner {
    pub fn name() -> &'static str {
        module_path!()
    }
}

fn main() {
    assert_eq!(module_path!(), "r#override");
    assert_eq!(inner::name(), "r#override::inner");
}
