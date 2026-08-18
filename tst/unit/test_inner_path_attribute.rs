// An inner `#![path]` names the directory a module's children are found in.
// An outer `#[path]` on the module already named one, and wins.
//@ crate-type: lib

mod with_inner_path_attr {
    #![path = "aux/inner_path_dir"]

    #[path = "child.rs"]
    pub mod inner;
}

#[path = "aux/inner_path_dir"]
mod with_both_path_attr {
    #![path = "this_is_ignored"]

    #[path = "child.rs"]
    pub mod inner;
}

pub fn sum() -> i32 {
    with_inner_path_attr::inner::value() + with_both_path_attr::inner::value()
}
