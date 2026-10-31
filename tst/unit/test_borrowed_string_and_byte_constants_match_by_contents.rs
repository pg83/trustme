const MESSAGE: &str = "crab";
const BYTES: &[u8] = &[17, 23, 41];
fn text(value: &str) -> bool { match value { MESSAGE => true, _ => false } }
fn bytes(value: &[u8]) -> bool { match value { BYTES => true, _ => false } }
fn main() {
    let owned = String::from("crab");
    assert!(text(&owned) && !text("claw"));
    assert!(bytes(&[17, 23, 41]) && !bytes(&[17, 23, 42]));
    assert!(!bytes(&[17, 23]));
}
