// Extracted from library/core/src/str/mod.rs:1493
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard";
    let x: &[_] = &['1', '2'];

    assert_eq!(s.rfind(x), None);
}
