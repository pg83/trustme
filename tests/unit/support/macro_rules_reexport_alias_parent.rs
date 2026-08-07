macro_rules! original {
    () => {
        42
    };
}

macro_rules! second_original {
    () => {
        1
    };
}

pub(crate) use {original as alias, second_original as second_alias};

#[path = "macro_rules_reexport_alias_child.rs"]
pub mod child;
