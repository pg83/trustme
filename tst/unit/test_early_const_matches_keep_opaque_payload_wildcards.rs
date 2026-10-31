const fn opaque(value: usize) -> impl Copy { value }
const fn choose(choice: u8) -> usize {
    let hidden = opaque(37);
    match (choice, hidden) { (0 | 1, _) => 3, _ => 5 }
}
type Selected = [u8; choose(1)];
fn main() {
    let value: Selected = [17; 3];
    assert_eq!(value, [17; 3]);
    assert_eq!((choose(0), choose(2)), (3, 5));
}
