//@ compile-fail: Cannot open file

fn main() {
    let _ = include_bytes!("test_include_bytes_rejects_a_path_inside_its_source.rs/child");
}
