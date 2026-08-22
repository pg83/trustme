const VALUE: i32 = match -1 {
    -1 => 3,
    4 => 5,
    _ => 0,
};

fn main() {
    assert_eq!(VALUE, 3);
}
