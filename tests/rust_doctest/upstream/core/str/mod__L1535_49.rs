// Extracted from library/core/src/str/mod.rs:1535
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "Mary had a little lamb".split(' ').collect();
    assert_eq!(v, ["Mary", "had", "a", "little", "lamb"]);
    
    let v: Vec<&str> = "".split('X').collect();
    assert_eq!(v, [""]);
    
    let v: Vec<&str> = "lionXXtigerXleopard".split('X').collect();
    assert_eq!(v, ["lion", "", "tiger", "leopard"]);
    
    let v: Vec<&str> = "lion::tiger::leopard".split("::").collect();
    assert_eq!(v, ["lion", "tiger", "leopard"]);
    
    let v: Vec<&str> = "AABBCC".split("DD").collect();
    assert_eq!(v, ["AABBCC"]);
    
    let v: Vec<&str> = "abc1def2ghi".split(char::is_numeric).collect();
    assert_eq!(v, ["abc", "def", "ghi"]);
    
    let v: Vec<&str> = "lionXtigerXleopard".split(char::is_uppercase).collect();
    assert_eq!(v, ["lion", "tiger", "leopard"]);
}
