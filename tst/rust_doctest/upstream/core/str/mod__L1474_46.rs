// Extracted from library/core/src/str/mod.rs:1474
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard Gepardi";

    assert_eq!(s.rfind('L'), Some(13));
    assert_eq!(s.rfind('é'), Some(14));
    assert_eq!(s.rfind("pard"), Some(24));
}
