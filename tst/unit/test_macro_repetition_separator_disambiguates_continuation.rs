macro_rules! separated_path {
    ($($path:ident)::+ $get:ident) => {
        fn main() {}
    };
}

separated_path!(proc_macro2::Group any_group);
