//@ edition: 2024

fn main() {
    let mut score = 0u16;
    if false || true {
        score += 11;
    }
    if true && false || true {
        score += 13;
    }
    if let Some(value) = Some(17u16) && value > 10 {
        score += value;
    }
    assert_eq!(score, 41);
}
