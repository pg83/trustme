//@ edition: 2021

fn main() {
    let first = true;
    let second = true;
    let third = true;
    assert!(if first && second && third {
        true
    } else {
        false
    });
}
