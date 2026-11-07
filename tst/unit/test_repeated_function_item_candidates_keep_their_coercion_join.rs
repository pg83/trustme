fn add_three(value: u16) -> u16 { value + 3 }
fn add_five(value: u16) -> u16 { value + 5 }

fn select(route: u8) -> impl Fn(u16) -> u16 + Copy {
    match route {
        0 => add_three,
        1 => add_three,
        _ => add_five,
    }
}

fn main() {
    assert_eq!(select(0)(269), 272);
    assert_eq!(select(1)(269), 272);
    assert_eq!(select(2)(269), 274);
}
