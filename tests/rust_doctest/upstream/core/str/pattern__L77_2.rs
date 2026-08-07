// Extracted from library/core/src/str/pattern.rs:77
#![allow(unused)]
fn main() {
    // &str
    assert_eq!("abaaa".find("ba"), Some(1));
    assert_eq!("abaaa".find("bac"), None);

    // char
    assert_eq!("abaaa".find('a'), Some(0));
    assert_eq!("abaaa".find('b'), Some(1));
    assert_eq!("abaaa".find('c'), None);

    // &[char; N]
    assert_eq!("ab".find(&['b', 'a']), Some(0));
    assert_eq!("abaaa".find(&['a', 'z']), Some(0));
    assert_eq!("abaaa".find(&['c', 'd']), None);

    // &[char]
    assert_eq!("ab".find(&['b', 'a'][..]), Some(0));
    assert_eq!("abaaa".find(&['a', 'z'][..]), Some(0));
    assert_eq!("abaaa".find(&['c', 'd'][..]), None);

    // FnMut(char) -> bool
    assert_eq!("abcdef_z".find(|ch| ch > 'd' && ch < 'y'), Some(4));
    assert_eq!("abcddd_z".find(|ch| ch > 'd' && ch < 'y'), None);
}
