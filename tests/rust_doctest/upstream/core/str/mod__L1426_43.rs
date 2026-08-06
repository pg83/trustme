// Extracted from library/core/src/str/mod.rs:1426
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard Gepardi";
    
    assert_eq!(s.find('L'), Some(0));
    assert_eq!(s.find('é'), Some(14));
    assert_eq!(s.find("pard"), Some(17));
}
