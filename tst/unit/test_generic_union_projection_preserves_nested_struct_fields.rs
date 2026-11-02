//@ compile-variant: -O
#[derive(Clone, Copy)]
struct Row<T: Copy> { pair: (T, T) }
union Choice<T: Copy> { row: Row<T>, plain: T }
fn update<T: Copy>(choice: &mut Choice<T>, value: T) -> T {
    unsafe { choice.row.pair.1 = value; choice.row.pair.0 }
}
fn main() {
    let mut choice = Choice { row: Row { pair: (17u16, 23) } };
    assert_eq!(update(&mut choice, 31), 17);
    assert_eq!(unsafe { choice.row.pair }, (17, 31));
}
