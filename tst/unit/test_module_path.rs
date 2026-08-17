// `module_path!` names the crate and the module it is written in. The crate
// name is derived from the input file when none is given, but that only
// happened after expansion -- by which time `module_path!` had already been
// expanded with an empty name.
//
// Same shape as the upstream test issues/issue-18859.rs.
mod foo {
    pub mod bar {
        pub mod baz {
            pub fn name() -> &'static str {
                module_path!()
            }
        }
    }
}

fn main() {
    assert_eq!(module_path!(), "test_module_path");
    assert_eq!(foo::bar::baz::name(), "test_module_path::foo::bar::baz");
}
