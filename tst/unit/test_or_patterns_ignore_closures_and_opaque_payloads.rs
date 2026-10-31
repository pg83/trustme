fn opaque(value: u32) -> impl Copy { value }
fn answer() -> u32 { 37 }
struct Unit;

fn main() {
    let closure = |value: u32| value + 3;
    let hidden = opaque(41);
    let function: fn() -> u32 = answer;
    for choice in [0u8, 1, 2] {
        let a = match (choice, &closure) { (0 | 1, &_) => 17, _ => 23 };
        let b = match (choice, hidden) { (0 | 1, _) => 17, _ => 23 };
        let c = match (choice, function, answer, Unit) { (0 | 1, _, _, _) => 17, _ => 23 };
        assert_eq!((a, b, c), if choice < 2 { (17, 17, 17) } else { (23, 23, 23) });
    }
    assert_eq!((closure(34), function()), (37, 37));
}
