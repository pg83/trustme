// Extracted from library/std/src/collections/hash/map.rs:986
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut libraries = HashMap::new();
    libraries.insert("Bodleian Library".to_string(), 1602);
    libraries.insert("Athenæum".to_string(), 1807);
    libraries.insert("Herzogin-Anna-Amalia-Bibliothek".to_string(), 1691);
    libraries.insert("Library of Congress".to_string(), 1800);
    
    // Get Athenæum and Bodleian Library
    let [Some(a), Some(b)] = libraries.get_disjoint_mut([
        "Athenæum",
        "Bodleian Library",
    ]) else { panic!() };
    
    // Assert values of Athenæum and Library of Congress
    let got = libraries.get_disjoint_mut([
        "Athenæum",
        "Library of Congress",
    ]);
    assert_eq!(
        got,
        [
            Some(&mut 1807),
            Some(&mut 1800),
        ],
    );
    
    // Missing keys result in None
    let got = libraries.get_disjoint_mut([
        "Athenæum",
        "New York Public Library",
    ]);
    assert_eq!(
        got,
        [
            Some(&mut 1807),
            None
        ]
    );
}
