//@ edition: 2015

fn main() {
    let name = String::from("argument");
    assert!(true, format!("Non-unique argument name: {} is already in use", name));
}
