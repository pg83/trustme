trait Read { fn read(&self) -> u16; }
#[derive(Clone, Copy)]
struct Number(u16);
impl Read for Number { fn read(&self) -> u16 { self.0 } }
fn make(value: u16) -> impl Copy + Read { Number(value) }

fn take<T>(value: Option<(u8, T)>) -> T {
    match value {
        Some((3..=7, item)) => item,
        Some((_, item)) => item,
        None => panic!("missing value"),
    }
}

fn main() {
    let value = make(257);
    let selected = take(std::hint::black_box(Some((5, value))));
    assert_eq!(selected.read(), 257);
    let selected_again = take(Some((9, make(519))));
    assert_eq!(selected_again.read(), 519);
}
