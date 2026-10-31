#[derive(PartialEq, Eq)]
struct Unit;
const TOKEN: Unit = Unit;
fn choose(choice: u8, value: Unit) -> u8 {
    match (choice, value) {
        (0, Unit) | (1, Unit {}) => 17,
        (2 | 3, _) => 23,
        (_, TOKEN) => 41,
    }
}
fn main() {
    assert_eq!([choose(0, Unit), choose(1, Unit {}), choose(2, TOKEN), choose(3, Unit), choose(4, Unit)], [17, 17, 23, 23, 41]);
}
