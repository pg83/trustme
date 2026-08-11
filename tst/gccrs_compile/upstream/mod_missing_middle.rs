// { dg-additional-options "-w" }


#[path = "mod_missing_middle/missing_middle/sub/mod.rs"]
mod missing_middle_sub;

#[path = "mod_missing_middle/missing_middle/explicit.not.rs"]
mod missing_middle_explicit;

#[path = "mod_missing_middle/missing_middle"]
mod with_outer_path_attr {
    #[path = "outer_path.rs"]
    mod inner;
}

mod with_inner_path_attr {
    #![path = "mod_missing_middle/missing_middle"]

    #[path = "inner_path.rs"]
    mod inner;
}

#[path = "mod_missing_middle/missing_middle"]
mod with_both_path_attr {
    #![path = "this_is_ignored"]

    #[path = "both_path.rs"]
    mod inner;
}
