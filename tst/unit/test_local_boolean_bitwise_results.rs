fn combine() -> [bool; 12] {
    let yes = true;
    let no = false;
    [yes & yes, yes & no, no & yes, no & no,
     yes | yes, yes | no, no | yes, no | no,
     yes ^ yes, yes ^ no, no ^ yes, no ^ no]
}
fn main() {
    assert_eq!(combine(), [true, false, false, false,
                          true, true, true, false,
                          false, true, true, false]);
}
