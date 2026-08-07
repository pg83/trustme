// Extracted from library/core/src/str/mod.rs:1853
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "Mary had a little lambda".splitn(3, ' ').collect();
    assert_eq!(v, ["Mary", "had", "a little lambda"]);

    let v: Vec<&str> = "lionXXtigerXleopard".splitn(3, "X").collect();
    assert_eq!(v, ["lion", "", "tigerXleopard"]);

    let v: Vec<&str> = "abcXdef".splitn(1, 'X').collect();
    assert_eq!(v, ["abcXdef"]);

    let v: Vec<&str> = "".splitn(1, 'X').collect();
    assert_eq!(v, [""]);
}
