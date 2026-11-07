fn returned() -> u16 {
    let answer = return 953 else { panic!("return cannot reach else") };
    answer
}

fn checked() -> Option<u16> {
    let 7 = Some(7u16)? else { return None };
    Some(13)
}

fn main() {
    let 7 = 3u16 + 4 else { panic!("wrong sum") };
    let true = !false else { panic!("wrong negation") };
    let number = *(&937u16) else { panic!("missing number") };
    let make = || Some(941u16) else { panic!("missing closure") };
    let mut assigned = None;
    let () = assigned = Some(947u16) else { panic!("assignment failed") };
    let bounds = 11u16.. else { panic!("missing range") };
    assert_eq!(number, 937);
    assert_eq!(make(), Some(941));
    assert_eq!(assigned, Some(947));
    assert_eq!(bounds.start, 11);
    assert_eq!(returned(), 953);
    assert_eq!(checked(), Some(13));
}
