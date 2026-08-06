// Extracted from library/core/src/str/mod.rs:1703
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "Mary had a little lamb".rsplit(' ').collect();
    assert_eq!(v, ["lamb", "little", "a", "had", "Mary"]);
    
    let v: Vec<&str> = "".rsplit('X').collect();
    assert_eq!(v, [""]);
    
    let v: Vec<&str> = "lionXXtigerXleopard".rsplit('X').collect();
    assert_eq!(v, ["leopard", "tiger", "", "lion"]);
    
    let v: Vec<&str> = "lion::tiger::leopard".rsplit("::").collect();
    assert_eq!(v, ["leopard", "tiger", "lion"]);
}
