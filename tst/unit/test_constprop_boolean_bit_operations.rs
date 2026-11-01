fn main() {
    let yes = true;
    let no = false;
    assert_eq!((yes & yes, yes & no, no & no), (true, false, false));
    assert_eq!((yes | yes, yes | no, no | no), (true, true, false));
    assert_eq!((yes ^ yes, yes ^ no, no ^ no), (false, true, false));
    assert_eq!((!yes, !no, yes == no, yes != no), (false, true, false, true));
    assert!(!no && yes);
}
