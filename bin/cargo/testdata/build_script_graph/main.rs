extern "C" {
    fn graph_native() -> i32;
}

fn main() {
    assert_eq!(unsafe { graph_native() }, 42);
}
