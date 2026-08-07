// Extracted from library/core/src/str/mod.rs:1905
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "Mary had a little lamb".rsplitn(3, ' ').collect();
    assert_eq!(v, ["lamb", "little", "Mary had a"]);

    let v: Vec<&str> = "lionXXtigerXleopard".rsplitn(3, 'X').collect();
    assert_eq!(v, ["leopard", "tiger", "lionX"]);

    let v: Vec<&str> = "lion::tiger::leopard".rsplitn(2, "::").collect();
    assert_eq!(v, ["leopard", "lion::tiger"]);
}
