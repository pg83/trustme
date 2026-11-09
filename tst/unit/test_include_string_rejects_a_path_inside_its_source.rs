//@ compile-fail: Cannot open file

fn main() {
    let _ = include_str!("test_include_string_rejects_a_path_inside_its_source.rs/child");
}
