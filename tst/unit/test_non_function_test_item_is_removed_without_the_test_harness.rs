#[test]
struct Hidden;

#[allow(non_snake_case)]
fn Hidden() -> u8 { 29 }

fn main() {
    assert_eq!(Hidden(), 29);
}
