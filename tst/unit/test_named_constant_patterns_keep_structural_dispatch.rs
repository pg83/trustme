#[derive(PartialEq, Eq)]
struct Mark { number: u16 }
const FIRST: Mark = Mark { number: 3 };
const PAIR: (u16, u16) = (5, 7);

fn main() {
    for (number, expected) in [(3, 11), (13, 17)] {
        assert_eq!(match (Mark { number }) { FIRST => 11, _ => 17 }, expected);
    }
    assert_eq!(match (5, 7) { PAIR => 19, _ => 23 }, 19);
    assert_eq!(match (29, 31) { PAIR => 37, _ => 41 }, 41);
}
