// Extracted from library/std/src/collections/hash/map.rs:1071
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut libraries = HashMap::new();
    libraries.insert("Bodleian Library".to_string(), 1602);
    libraries.insert("Athenæum".to_string(), 1807);
    libraries.insert("Herzogin-Anna-Amalia-Bibliothek".to_string(), 1691);
    libraries.insert("Library of Congress".to_string(), 1800);
    
    // SAFETY: The keys do not overlap.
    let [Some(a), Some(b)] = (unsafe { libraries.get_disjoint_unchecked_mut([
        "Athenæum",
        "Bodleian Library",
    ]) }) else { panic!() };
    
    // SAFETY: The keys do not overlap.
    let got = unsafe { libraries.get_disjoint_unchecked_mut([
        "Athenæum",
        "Library of Congress",
    ]) };
    assert_eq!(
        got,
        [
            Some(&mut 1807),
            Some(&mut 1800),
        ],
    );
    
    // SAFETY: The keys do not overlap.
    let got = unsafe { libraries.get_disjoint_unchecked_mut([
        "Athenæum",
        "New York Public Library",
    ]) };
    // Missing keys result in None
    assert_eq!(got, [Some(&mut 1807), None]);
}
