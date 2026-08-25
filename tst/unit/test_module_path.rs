// `module_path!` names the crate and the module it is written in. The crate
// name is derived from the input file when none is given, but that only
// happened after expansion -- by which time `module_path!` had already been
// expanded with an empty name.
//
// A crate named after a reserved word is written the only way it can be, as a
// raw identifier.
//
// Same shape as the upstream tests issues/issue-18859.rs and
// attributes/z-crate-attr/respect-existing-attrs.rs.
mod foo {
    pub mod bar {
        pub mod baz {
            pub fn name() -> &'static str {
                module_path!()
            }
        }
    }
}

macro_rules! nested_module_path {
    () => {
        module_path!()
    };
}

fn main() {
    assert_eq!(module_path!(), "test_module_path");
    assert_eq!(foo::bar::baz::name(), "test_module_path::foo::bar::baz");

    // Function bodies use anonymous AST modules internally. A builtin macro
    // reached through a local macro expansion must not expose that module.
    macro_rules! local_wrapper {
        () => {
            nested_module_path!()
        };
    }
    assert_eq!(local_wrapper!(), "test_module_path");
}
