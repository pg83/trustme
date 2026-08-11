macro_rules! file_name {
    () => {
        "eager3.rs"
    };
}

pub const SOURCE: &str = include_str!(file_name!());
