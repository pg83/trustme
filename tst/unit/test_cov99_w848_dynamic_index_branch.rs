#[inline(never)]
fn choose(flags: &[[bool; 2]; 2], row: usize, column: usize) -> u8 {
    if flags[row][column] {
        11
    } else {
        7
    }
}

fn main() {
    let flags = [[false, true], [true, false]];
    assert_eq!(choose(&flags, 0, 0), 7);
    assert_eq!(choose(&flags, 0, 1), 11);
    assert_eq!(choose(&flags, 1, 0), 11);
    assert_eq!(choose(&flags, 1, 1), 7);
}
