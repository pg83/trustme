fn 𐐀(𝒜: u16) -> u16 { 𝒜 + 1 }
fn main() {
    let A = 199u16;
    let 𝐀 = 211u16;
    assert_eq!(𐐀(𝐀), 212);
    assert_eq!(A, 199);
    assert_ne!(A, 𝐀);
}
