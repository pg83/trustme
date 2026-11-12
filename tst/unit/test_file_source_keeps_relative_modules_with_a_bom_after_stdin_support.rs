//@ input: aux/stdin_file_mode_fixture.rs

#[path = "aux/stdin_file_mode_fixture.rs"]
mod loaded;

fn main() {
    assert_eq!(loaded::VALUE, 857);
    assert_eq!(loaded::label(), "файл");
}
